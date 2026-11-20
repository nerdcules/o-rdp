#include "backend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHostAddress>
#include <QRegularExpression>
#include <algorithm>

#include "rdpsession.h"

Backend::Backend(const QString &known, Keyring keyring, QObject *parent)
    : QObject(parent), m_known(known), m_keyring(std::move(keyring)), m_settings("o-rdp", "o-rdp") {
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
    // Text copied here goes to every open session's PC.
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        connect(clipboard, &QClipboard::dataChanged, this, [this, clipboard] {
            const QString text = clipboard->text();
            for (RdpSession *session : std::as_const(m_sessions))
                session->setLocalClipboard(text);
        });
}

Backend::~Backend() {
    // Each session's thread is stopped and joined as it goes.
    qDeleteAll(m_sessions);
}

QObject *Backend::session() const { return m_sessions.value(m_shown); }

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
    if (address.isEmpty() || user.isEmpty())
        return;
    if (m_sessions.contains(address)) {
        show(address);
        return;
    }
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
    options.extra = qEnvironmentVariable("RDP_OPTS");
    // Full screen is the window's; the password goes into the connection's
    // settings in memory, so it is no argument at all.
    m_lastArguments = freerdpArguments(options);

    auto *session = new RdpSession(address, m_lastArguments, password, this);
    m_sessions.insert(address, session);
    connect(session, &RdpSession::finished, this, [this, address](int code, const QString &output) {
        ended(address, code, output);
    });
    connect(session, &RdpSession::remoteClipboard, this, [](const QString &text) {
        if (QClipboard *clipboard = QGuiApplication::clipboard())
            clipboard->setText(text);
    });
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        session->setLocalClipboard(clipboard->text());
    m_shown = address;
    emit sessionChanged();
    emit hostsChanged();
    emit connected(address);
}

void Backend::show(const QString &address) {
    const QString next = m_sessions.contains(address) ? address : QString();
    if (next == m_shown)
        return;
    m_shown = next;
    emit sessionChanged();
}

void Backend::disconnectFrom(const QString &address) {
    if (RdpSession *session = m_sessions.value(address))
        session->stop();
}

void Backend::ended(const QString &address, int exitCode, const QString &output) {
    RdpSession *session = m_sessions.take(address);
    if (m_shown == address) {
        // Another open session takes its place, or the list does.
        m_shown = m_sessions.isEmpty() ? QString() : m_sessions.constBegin().key();
        emit sessionChanged();
    }
    if (session)
        session->deleteLater();
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
