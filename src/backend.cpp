#include "backend.h"

#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

Backend::Backend(const QString &known, const QString &program, Keyring keyring, QObject *parent)
    : QObject(parent), m_known(known),
      m_program(program.isEmpty() ? QStandardPaths::findExecutable("xfreerdp3") : program),
      m_keyring(std::move(keyring)), m_settings("o-rdp", "o-rdp") {
    connect(&m_scanner, &Scanner::found, this, [this](const QString &address) {
        m_online.insert(address);
        // Remembered as rdp-connect does, so the script sees it too.
        m_known.remember(address, QString(), QString());
        emit hostsChanged();
    });
    connect(&m_scanner, &Scanner::named, this, [this](const QString &address, const QString &name) {
        m_names.insert(address, name);
        if (m_known.find(address).name.isEmpty() || m_known.find(address).name == "?")
            m_known.remember(address, name, QString());
        emit hostsChanged();
    });
    connect(&m_scanner, &Scanner::progress, this, &Backend::scanChanged);
    connect(&m_scanner, &Scanner::finished, this, [this] {
        m_scannedOnce = true;
        emit scanChanged();
        emit hostsChanged();
    });
}

QVariantList Backend::hosts() const {
    struct Row {
        QString address, name, user;
        bool online, connected;
    };
    QList<Row> rows;
    for (const KnownHost &host : m_known.all()) {
        QString name = host.name == "?" ? QString() : host.name;
        if (name.isEmpty())
            name = m_names.value(host.address);
        rows.append({host.address, name, host.user, m_online.contains(host.address), m_sessions.contains(host.address)});
    }
    // Found by the scan, on the screen first; then by address.
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.online != b.online)
            return a.online;
        bool okA = false, okB = false;
        const quint32 ia = QHostAddress(a.address).toIPv4Address(&okA), ib = QHostAddress(b.address).toIPv4Address(&okB);
        if (okA && okB)
            return ia < ib;
        return a.address < b.address;
    });
    QVariantList list;
    for (const Row &row : rows)
        list.append(QVariantMap{{"address", row.address}, {"name", row.name}, {"user", row.user},
                                {"online", row.online}, {"connected", row.connected}});
    return list;
}

void Backend::setFullscreen(bool on) {
    m_settings.setValue("fullscreen", on);
    emit optionsChanged();
}

void Backend::setAllMonitors(bool on) {
    m_settings.setValue("allMonitors", on);
    emit optionsChanged();
}

void Backend::scan() {
    m_online.clear();
    m_scanner.start();
    emit scanChanged();
    emit hostsChanged();
}

QString Backend::savedPassword(const QString &address, const QString &user) const {
    if (user.isEmpty() || !m_keyring.available())
        return {};
    return m_keyring.lookup(address, user);
}

void Backend::connectTo(const QString &address, const QString &user, const QString &password, bool remember) {
    if (m_program.isEmpty() || address.isEmpty() || user.isEmpty() || m_sessions.contains(address))
        return;
    m_known.remember(address, QString(), user);
    if (m_keyring.available()) {
        if (remember && !password.isEmpty())
            m_keyring.store(address, user, password);
        else if (!remember)
            m_keyring.clear(address, user);
    }
    ConnectOptions options;
    options.address = address;
    options.user = user;
    options.password = password;
    options.fullscreen = fullscreen();
    options.allMonitors = allMonitors();
    options.extra = qEnvironmentVariable("RDP_OPTS");
    const QStringList arguments = freerdpArguments(options);
    // Recorded for tests without the password.
    m_lastArguments = arguments;
    m_lastArguments.replaceInStrings(QRegularExpression("^/p:.*$"), "/p:***");

    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_sessions.insert(address, process);
    connect(process, &QProcess::finished, this, [this, process, address](int code, QProcess::ExitStatus status) {
        ended(address, status == QProcess::NormalExit ? code : -1, QString::fromUtf8(process->readAll()));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, address](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            ended(address, -1, tr("FreeRDP could not be started: %1").arg(process->errorString()));
    });
    // Every argument goes in through stdin, one per line: the password
    // never appears in a command line other programs can read.
    process->start(m_program, {"/args-from:stdin"});
    process->write(arguments.join('\n').toUtf8() + '\n');
    process->closeWriteChannel();
    emit hostsChanged();
    emit connected(address);
}

void Backend::ended(const QString &address, int exitCode, const QString &output) {
    QProcess *process = m_sessions.take(address);
    if (process)
        process->deleteLater();
    const QString problem = connectionProblem(exitCode, output);
    if (!problem.isEmpty()) {
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        m_problem = QVariantMap{{"address", address}, {"text", problem}, {"details", lines.mid(qMax(0, int(lines.size()) - 6)).join('\n')}};
        emit problemChanged();
    }
    emit hostsChanged();
}

bool Backend::addAddress(const QString &text) {
    const QString address = text.trimmed();
    static const QRegularExpression hostname(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?(:\\d{1,5})?$"));
    if (address.isEmpty() || !hostname.match(address).hasMatch())
        return false;
    m_known.remember(address, QString(), QString());
    emit hostsChanged();
    return true;
}

void Backend::forget(const QString &address) {
    m_known.forget(address);
    m_online.remove(address);
    emit hostsChanged();
}

void Backend::clearProblem() {
    m_problem.clear();
    emit problemChanged();
}
