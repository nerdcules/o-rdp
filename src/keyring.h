#pragma once

#include <QString>
#include <functional>

// RDP passwords in the GNOME keyring, through secret-tool, kept per host and
// user under service "o-rdp". Only when the user ticks "Remember".
class Keyring {
public:
    // run(arguments, stdin) -> (exit code, stdout); tests pass a fake.
    using Run = std::function<std::pair<int, QString>(const QStringList &, const QByteArray &)>;
    explicit Keyring(Run run = {});

    bool available() const;
    QString lookup(const QString &host, const QString &user) const;
    bool store(const QString &host, const QString &user, const QString &password) const;
    bool clear(const QString &host, const QString &user) const;

private:
    Run m_run;
};
