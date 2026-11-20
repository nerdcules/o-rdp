#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include "hosts.h"
#include "keyring.h"
#include "scanner.h"

class RdpSession;

// The window's state: the hosts (known ones and those the last scan found),
// the scan's progress, the sessions open (one of them on show), and what
// went wrong with the last one.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QString subnet READ subnet NOTIFY scanChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(int probed READ probed NOTIFY scanChanged)
    Q_PROPERTY(int total READ total NOTIFY scanChanged)
    Q_PROPERTY(bool scannedOnce READ scannedOnce NOTIFY scanChanged)
    Q_PROPERTY(bool keyringAvailable READ keyringAvailable CONSTANT)
    Q_PROPERTY(QVariantMap problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY optionsChanged)
    // The session on show in the window, or null.
    Q_PROPERTY(QObject *session READ session NOTIFY sessionChanged)

public:
    // known: the hosts file.
    explicit Backend(const QString &known = {}, Keyring keyring = Keyring(), QObject *parent = nullptr);
    ~Backend() override;

    QVariantList hosts() const;
    QString subnet() const { return m_scanner.subnet().cidr(); }
    bool scanning() const { return m_scanner.running(); }
    int probed() const { return m_scanner.probed(); }
    int total() const { return m_scanner.total(); }
    bool scannedOnce() const { return m_scannedOnce; }
    bool keyringAvailable() const { return m_keyring.available(); }
    QVariantMap problem() const { return m_problem; }
    bool fullscreen() const { return m_settings.value("fullscreen", false).toBool(); }
    void setFullscreen(bool on);
    QObject *session() const;

    Q_INVOKABLE void scan();
    // The password the keyring holds for this host and user, or "".
    Q_INVOKABLE QString savedPassword(const QString &address, const QString &user) const;
    // Opens a session inside the window and shows it. remember stores the
    // password in the keyring (or removes a stored one when unticked).
    Q_INVOKABLE void connectTo(const QString &address, const QString &user, const QString &password, bool remember);
    // Shows an open session; "" shows none.
    Q_INVOKABLE void show(const QString &address);
    Q_INVOKABLE void disconnectFrom(const QString &address);
    // Adds an address or name typed by hand; false if it isn't one.
    Q_INVOKABLE bool addAddress(const QString &text);
    Q_INVOKABLE void forget(const QString &address);
    Q_INVOKABLE void clearProblem();

    // What the last session was given, without the password; for tests.
    QStringList lastArguments() const { return m_lastArguments; }

signals:
    void hostsChanged();
    void scanChanged();
    void problemChanged();
    void optionsChanged();
    void sessionChanged();
    void connected(const QString &address);

private:
    void ended(const QString &address, int exitCode, const QString &output);

    KnownHosts m_known;
    Keyring m_keyring;
    Scanner m_scanner;
    QSet<QString> m_online;
    QHash<QString, QString> m_names;
    QHash<QString, RdpSession *> m_sessions;
    QString m_shown;
    QVariantMap m_problem;
    QSettings m_settings;
    QStringList m_lastArguments;
    bool m_scannedOnce = false;
};
