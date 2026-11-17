#include "keyring.h"

#include <QProcess>
#include <QStandardPaths>

static std::pair<int, QString> runSecretTool(const QStringList &arguments, const QByteArray &input) {
    QProcess process;
    process.start("secret-tool", arguments);
    if (!process.waitForStarted(3000))
        return {-1, {}};
    // The password goes in on stdin, never as an argument.
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(10000)) {
        process.kill();
        return {-1, {}};
    }
    return {process.exitCode(), QString::fromUtf8(process.readAllStandardOutput())};
}

Keyring::Keyring(Run run) : m_run(run ? std::move(run) : Run(runSecretTool)) {}

bool Keyring::available() const {
    return !QStandardPaths::findExecutable("secret-tool").isEmpty();
}

QString Keyring::lookup(const QString &host, const QString &user) const {
    const auto [code, out] = m_run({"lookup", "service", "o-rdp", "host", host, "user", user}, {});
    return code == 0 ? out : QString();
}

bool Keyring::store(const QString &host, const QString &user, const QString &password) const {
    const QString label = QString("o-rdp %1@%2").arg(user, host);
    return m_run({"store", "--label", label, "service", "o-rdp", "host", host, "user", user}, password.toUtf8()).first == 0;
}

bool Keyring::clear(const QString &host, const QString &user) const {
    return m_run({"clear", "service", "o-rdp", "host", host, "user", user}, {}).first == 0;
}
