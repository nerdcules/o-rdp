#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Hosts o-rdp knows, in the file rdp-connect keeps
// (~/.local/state/rdp-connect/known): one "ip<TAB>name<TAB>user" per line,
// so the terminal script and the window share names and last users.
struct KnownHost {
    QString address;
    QString name;
    QString user;
};

class KnownHosts {
public:
    // path defaults to ~/.local/state/rdp-connect/known; tests pass their own.
    explicit KnownHosts(const QString &path = {});

    QList<KnownHost> all() const;
    KnownHost find(const QString &address) const;
    // Adds or replaces address's line, keeping the others in order. An empty
    // name or user keeps what was there.
    bool remember(const QString &address, const QString &name, const QString &user);
    bool forget(const QString &address);
    QString path() const { return m_path; }

private:
    bool write(const QList<KnownHost> &hosts) const;
    QString m_path;
};

// The local subnet to scan: the interface that carries the default route
// (so a VPN's /32 isn't picked), as address and prefix length.
struct Subnet {
    QString interfaceName;
    QString address;
    int prefix = 0;
    bool valid() const { return !address.isEmpty() && prefix > 0; }
    // The network, not this computer's address: 192.168.8.0/22.
    QString cidr() const;
};
Subnet defaultSubnet(const QString &routeTable = {});
// The interface of the default route with the lowest metric, from
// /proc/net/route's text.
QString defaultRouteDevice(const QString &routeTable);

// Every host address in the subnet, network and broadcast left out. Like
// rdp-connect, anything larger than a /20 is cut to the /24 around the
// computer's own address.
QStringList hostAddresses(const Subnet &subnet);

// A NetBIOS node-status query (UDP 137) and the computer name in its
// answer: Windows answers with its own name.
QByteArray netbiosQuery();
QString netbiosName(const QByteArray &answer);

// FreeRDP's arguments for one connection. The window hands them to
// libfreerdp in memory, without the password; `o-rdp connect` writes them,
// one per line, to `xfreerdp3 /args-from:stdin`, so the password goes
// through a pipe, never on a command line other programs can see.
struct ConnectOptions {
    QString address;
    QString user;
    QString password;
    bool fullscreen = false;
    bool allMonitors = false;
    QString extra; // $RDP_OPTS, split on spaces as the script did
};
QStringList freerdpArguments(const ConnectOptions &options);

// What went wrong, in plain words, from FreeRDP's output and exit code;
// empty when nothing did.
QString connectionProblem(int exitCode, const QString &output);
