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

class QProcess;

// The window's state: the hosts (known ones and those the last scan found),
// the scan's progress, the connections running, and what went wrong with
// the last one.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QString subnet READ subnet NOTIFY scanChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(int probed READ probed NOTIFY scanChanged)
    Q_PROPERTY(int total READ total NOTIFY scanChanged)
    Q_PROPERTY(bool scannedOnce READ scannedOnce NOTIFY scanChanged)
    Q_PROPERTY(bool freerdpMissing READ freerdpMissing CONSTANT)
    Q_PROPERTY(bool keyringAvailable READ keyringAvailable CONSTANT)
    Q_PROPERTY(QVariantMap problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY optionsChanged)
    Q_PROPERTY(bool allMonitors READ allMonitors WRITE setAllMonitors NOTIFY optionsChanged)

public:
    // known: the hosts file; program: the FreeRDP client (xfreerdp3).
    explicit Backend(const QString &known = {}, const QString &program = {}, Keyring keyring = Keyring(),
                     QObject *parent = nullptr);

    QVariantList hosts() const;
    QString subnet() const { return m_scanner.subnet().cidr(); }
    bool scanning() const { return m_scanner.running(); }
    int probed() const { return m_scanner.probed(); }
    int total() const { return m_scanner.total(); }
    bool scannedOnce() const { return m_scannedOnce; }
    bool freerdpMissing() const { return m_program.isEmpty(); }
    bool keyringAvailable() const { return m_keyring.available(); }
    QVariantMap problem() const { return m_problem; }
    bool fullscreen() const { return m_settings.value("fullscreen", false).toBool(); }
    bool allMonitors() const { return m_settings.value("allMonitors", false).toBool(); }
    void setFullscreen(bool on);
    void setAllMonitors(bool on);

    Q_INVOKABLE void scan();
    // The password the keyring holds for this host and user, or "".
    Q_INVOKABLE QString savedPassword(const QString &address, const QString &user) const;
    // Starts FreeRDP. remember stores the password in the keyring (or
    // removes a stored one when unticked).
    Q_INVOKABLE void connectTo(const QString &address, const QString &user, const QString &password, bool remember);
    // Adds an address or name typed by hand; false if it isn't one.
    Q_INVOKABLE bool addAddress(const QString &text);
    Q_INVOKABLE void forget(const QString &address);
    Q_INVOKABLE void clearProblem();

    // The process FreeRDP runs in, and what it is given; public for tests.
    QStringList lastArguments() const { return m_lastArguments; }

signals:
    void hostsChanged();
    void scanChanged();
    void problemChanged();
    void optionsChanged();
    void connected(const QString &address);

private:
    void ended(const QString &address, int exitCode, const QString &output);

    KnownHosts m_known;
    QString m_program;
    Keyring m_keyring;
    Scanner m_scanner;
    QSet<QString> m_online;
    QHash<QString, QString> m_names;
    QHash<QString, QProcess *> m_sessions;
    QVariantMap m_problem;
    QSettings m_settings;
    QStringList m_lastArguments;
    bool m_scannedOnce = false;
};
