#include "hosts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QtEndian>
#include <climits>

namespace {

quint32 toInt(const QString &address) {
    bool ok = false;
    const quint32 value = QHostAddress(address).toIPv4Address(&ok);
    return ok ? value : 0;
}

QString toAddress(quint32 value) {
    return QHostAddress(value).toString();
}

} // namespace

KnownHosts::KnownHosts(const QString &path)
    : m_path(path.isEmpty() ? QDir::homePath() + "/.local/state/rdp-connect/known" : path) {}

QList<KnownHost> KnownHosts::all() const {
    QList<KnownHost> hosts;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return hosts;
    for (const QString &line : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = line.split('\t');
        if (fields.value(0).trimmed().isEmpty())
            continue;
        hosts.append({fields.value(0).trimmed(), fields.value(1).trimmed(), fields.value(2).trimmed()});
    }
    return hosts;
}

KnownHost KnownHosts::find(const QString &address) const {
    for (const KnownHost &host : all())
        if (host.address == address)
            return host;
    return {};
}

bool KnownHosts::remember(const QString &address, const QString &name, const QString &user) {
    QList<KnownHost> hosts = all();
    for (KnownHost &host : hosts) {
        if (host.address != address)
            continue;
        if (!name.isEmpty())
            host.name = name;
        if (!user.isEmpty())
            host.user = user;
        return write(hosts);
    }
    hosts.append({address, name, user});
    return write(hosts);
}

bool KnownHosts::forget(const QString &address) {
    QList<KnownHost> hosts = all();
    const qsizetype before = hosts.size();
    hosts.removeIf([&](const KnownHost &host) { return host.address == address; });
    return hosts.size() != before && write(hosts);
}

bool KnownHosts::write(const QList<KnownHost> &hosts) const {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // Written beside the file and renamed, as rdp-connect does with .tmp.
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    for (const KnownHost &host : hosts)
        file.write(QString("%1\t%2\t%3\n").arg(host.address, host.name, host.user).toUtf8());
    return file.commit();
}

QString Subnet::cidr() const {
    if (!valid())
        return {};
    const quint32 mask = prefix >= 32 ? 0xFFFFFFFFu : ~((quint32(1) << (32 - prefix)) - 1);
    return QString("%1/%2").arg(toAddress(toInt(address) & mask)).arg(prefix);
}

QString defaultRouteDevice(const QString &table) {
    QString device;
    int bestMetric = INT_MAX;
    for (const QString &line : table.split('\n')) {
        const QStringList f = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        // Destination and mask 00000000: the default route.
        if (f.size() < 8 || f.at(1) != "00000000" || f.at(7) != "00000000")
            continue;
        const int metric = f.at(6).toInt();
        if (metric < bestMetric) {
            bestMetric = metric;
            device = f.at(0);
        }
    }
    return device;
}

Subnet defaultSubnet(const QString &routeTable) {
    QString table = routeTable;
    if (table.isEmpty()) {
        QFile file("/proc/net/route");
        if (file.open(QIODevice::ReadOnly))
            table = QString::fromUtf8(file.readAll());
    }
    const QString device = defaultRouteDevice(table);
    Subnet subnet;
    subnet.interfaceName = device;
    if (device.isEmpty())
        return subnet;
    const QNetworkInterface interface = QNetworkInterface::interfaceFromName(device);
    for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
        if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol || entry.ip().isLoopback())
            continue;
        subnet.address = entry.ip().toString();
        subnet.prefix = entry.prefixLength();
        break;
    }
    return subnet;
}

QStringList hostAddresses(const Subnet &subnet) {
    QStringList addresses;
    if (!subnet.valid() || subnet.prefix > 30)
        return addresses;
    const quint32 own = toInt(subnet.address);
    const quint64 size = quint64(1) << (32 - subnet.prefix);
    quint32 first, last;
    if (size > 4096) {
        first = (own & ~quint32(255)) + 1;
        last = first + 253;
    } else {
        const quint32 network = own & ~quint32(size - 1);
        first = network + 1;
        last = network + quint32(size) - 2;
    }
    for (quint32 value = first; value <= last; ++value)
        addresses.append(toAddress(value));
    return addresses;
}

QByteArray netbiosQuery() {
    // Transaction 0x8228, one question for "*" (CKAAAA...), type NBSTAT.
    QByteArray packet = QByteArray::fromHex("822800000001000000000000");
    packet += char(0x20);
    packet += "CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    packet += char(0);
    packet += QByteArray::fromHex("00210001");
    return packet;
}

QString netbiosName(const QByteArray &answer) {
    // 12-byte header, the 34-byte question name, type, class, TTL and
    // length bring the name count to offset 56; 18 bytes per name after it.
    if (answer.size() < 57)
        return {};
    const int count = quint8(answer.at(56));
    int offset = 57;
    for (int i = 0; i < count && offset + 18 <= answer.size(); ++i, offset += 18) {
        const QString name = QString::fromLatin1(answer.mid(offset, 15)).trimmed();
        const quint8 type = quint8(answer.at(offset + 15));
        const quint16 flags = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(answer.constData() + offset + 16));
        // The workstation name (type 0) that isn't a group name.
        if (type == 0 && !(flags & 0x8000) && !name.isEmpty())
            return name;
    }
    return {};
}

QStringList freerdpArguments(const ConnectOptions &options) {
    // rdp-connect's defaults: a window that follows its size, trust on first
    // use for the certificate, sound and clipboard, and reconnect.
    QStringList args{"/v:" + options.address, "/u:" + options.user, "/dynamic-resolution", "/cert:tofu",
                     "/sound",                 "/clipboard",           "/network:lan",          "+auto-reconnect"};
    if (!options.password.isEmpty())
        args.append("/p:" + options.password);
    if (options.fullscreen)
        args.append("/f");
    if (options.allMonitors)
        args.append("/multimon");
    for (const QString &extra : options.extra.split(' ', Qt::SkipEmptyParts))
        args.append(extra);
    return args;
}

QString connectionProblem(int exitCode, const QString &output) {
    auto has = [&](const char *text) { return output.contains(QLatin1String(text), Qt::CaseInsensitive); };
    if (has("LOGON_FAILURE") || has("STATUS_LOGON_FAILURE") || has("ERRCONNECT_AUTHENTICATION_FAILED"))
        return QObject::tr("The PC turned down the user name or password.");
    if (has("PASSWORD_MISSING"))
        return QObject::tr("The PC wants a password.");
    if (has("ACCOUNT_LOCKED_OUT"))
        return QObject::tr("The account is locked out on that PC.");
    if (has("ACCOUNT_DISABLED"))
        return QObject::tr("The account is disabled on that PC.");
    if (has("PASSWORD_EXPIRED") || has("PASSWORD_MUST_CHANGE"))
        return QObject::tr("The password has expired; change it on the PC first.");
    if (has("certificate") && (has("changed") || has("mismatch")))
        return QObject::tr("The PC's certificate is not the one seen last time. If the PC was reinstalled that's expected; otherwise, be careful.");
    if (has("ERRCONNECT_CONNECT_FAILED") || has("Connection refused") || has("No route to host") ||
        has("ERRCONNECT_DNS_NAME_NOT_FOUND") || has("Connection timed out"))
        return QObject::tr("Couldn't reach the PC on port 3389. Is it awake, with Remote Desktop turned on?");
    if (has("ERRCONNECT_CONNECT_TRANSPORT_FAILED"))
        return QObject::tr("The connection to the PC broke off.");
    // 0 is a normal end; 1 and 2 are the server ending the session
    // (disconnected or logged off), also normal from here.
    if (exitCode == 0 || exitCode == 1 || exitCode == 2)
        return {};
    QString last;
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts))
        if (line.contains("[ERROR]"))
            last = line.section("]: ", -1).trimmed();
    return last.isEmpty() ? QObject::tr("The connection ended with an error (FreeRDP exit code %1).").arg(exitCode)
                          : QObject::tr("The connection ended with an error: %1").arg(last);
}
