#include "cli.h"

#include "hosts.h"
#include "keyring.h"
#include "scanner.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <termios.h>
#include <unistd.h>

namespace {

const QStringList kCommands{"scan", "list", "connect", "skill", "help", "--help", "-h"};

void usage(QTextStream &out) {
    out << "o-rdp - find the PCs with Remote Desktop on the local network and connect to them\n\n"
           "  o-rdp [open]                 open the window\n"
           "  o-rdp scan [--json]          the PCs with port 3389 open on this subnet, with names\n"
           "  o-rdp list [--json]          the PCs o-rdp and rdp-connect know, with the last user\n"
           "  o-rdp connect HOST [--user U]\n"
           "                               connect with FreeRDP; the password comes from the keyring or\n"
           "                               is asked here, and reaches FreeRDP through a pipe\n"
           "  o-rdp skill install          install the agent skill into ~/.claude/skills/o-rdp\n\n"
           "Known hosts are shared with rdp-connect (~/.local/state/rdp-connect/known).\n"
           "Extra FreeRDP flags come from $RDP_OPTS. Commands exit 0 on success, 1 with the problem on stderr.\n";
}

QString option(const QStringList &arguments, const QString &name) {
    const int at = int(arguments.indexOf(name));
    return at > 0 ? arguments.value(at + 1) : QString();
}

int scan(bool json, QTextStream &out, QTextStream &err) {
    Scanner scanner;
    KnownHosts known;
    QStringList found;
    QHash<QString, QString> names;
    QEventLoop loop;
    QObject::connect(&scanner, &Scanner::found, [&](const QString &address) {
        found.append(address);
        known.remember(address, QString(), QString());
    });
    QObject::connect(&scanner, &Scanner::named, [&](const QString &address, const QString &name) {
        names.insert(address, name);
        if (known.find(address).name.isEmpty() || known.find(address).name == "?")
            known.remember(address, name, QString());
    });
    QObject::connect(&scanner, &Scanner::finished, &loop, [&] {
        // Names arrive up to a few seconds after the last probe.
        QTimer::singleShot(found.isEmpty() ? 0 : 3200, &loop, &QEventLoop::quit);
    });
    scanner.start();
    if (!scanner.subnet().valid()) {
        err << "No network interface with a default route.\n";
        return 1;
    }
    if (!json)
        err << "Scanning " << scanner.subnet().cidr() << " for Remote Desktop (port 3389)...\n";
    err.flush();
    loop.exec();
    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) {
        return QHostAddress(a).toIPv4Address() < QHostAddress(b).toIPv4Address();
    });
    if (json) {
        QJsonArray list;
        for (const QString &address : found)
            list.append(QJsonObject{{"address", address}, {"name", names.value(address, known.find(address).name)}});
        out << QJsonDocument(QJsonObject{{"subnet", scanner.subnet().cidr()}, {"hosts", list}}).toJson();
        return 0;
    }
    if (found.isEmpty())
        out << "No PC with port 3389 open on " << scanner.subnet().cidr()
            << ". Check that Remote Desktop is on and the PC is awake.\n";
    for (const QString &address : found)
        out << address.leftJustified(16) << names.value(address, known.find(address).name) << "\n";
    return 0;
}

int list(bool json, QTextStream &out) {
    const QList<KnownHost> hosts = KnownHosts().all();
    if (json) {
        QJsonArray array;
        for (const KnownHost &host : hosts)
            array.append(QJsonObject{{"address", host.address}, {"name", host.name}, {"user", host.user}});
        out << QJsonDocument(array).toJson();
        return 0;
    }
    for (const KnownHost &host : hosts)
        out << host.address.leftJustified(16) << host.name.leftJustified(20) << host.user << "\n";
    return 0;
}

QString askPassword(QTextStream &err, const QString &prompt) {
    err << prompt;
    err.flush();
    termios old {};
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios quiet = old;
        quiet.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    QTextStream in(stdin);
    const QString password = in.readLine();
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        err << "\n";
    }
    return password;
}

int connectHost(const QStringList &arguments, QTextStream &out, QTextStream &err) {
    const QString host = arguments.value(2);
    if (host.isEmpty() || host.startsWith("--")) {
        err << "Usage: o-rdp connect HOST [--user U]\n";
        return 1;
    }
    const QString program = QStandardPaths::findExecutable("xfreerdp3");
    if (program.isEmpty()) {
        err << "FreeRDP (xfreerdp3) is not installed: omarchy-pkg-add freerdp\n";
        return 1;
    }
    KnownHosts known;
    QString user = option(arguments, "--user");
    if (user.isEmpty())
        user = known.find(host).user;
    if (user.isEmpty()) {
        err << "No user for " << host << ": pass --user\n";
        return 1;
    }
    known.remember(host, QString(), user);
    Keyring keyring;
    QString password = keyring.available() ? keyring.lookup(host, user) : QString();
    if (password.isEmpty())
        password = askPassword(err, QString("Password for %1@%2: ").arg(user, host));
    ConnectOptions options{host, user, password, false, false, qEnvironmentVariable("RDP_OPTS")};
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, {"/args-from:stdin"});
    process.write(freerdpArguments(options).join('\n').toUtf8() + '\n');
    process.closeWriteChannel();
    out << "Connecting to " << host << " as " << user << "...\n";
    out.flush();
    process.waitForFinished(-1);
    const QString problem = connectionProblem(process.exitCode(), QString());
    if (!problem.isEmpty()) {
        err << problem << "\n";
        return 1;
    }
    return 0;
}

int installSkill(QTextStream &out, QTextStream &err) {
    QFile source(":/skill/SKILL.md");
    if (!source.open(QIODevice::ReadOnly)) {
        err << "The bundled skill is missing from this build.\n";
        return 1;
    }
    const QString directory = QDir::homePath() + "/.claude/skills/o-rdp";
    QDir().mkpath(directory);
    QFile target(directory + "/SKILL.md");
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate) || target.write(source.readAll()) < 0) {
        err << "Could not write " << target.fileName() << "\n";
        return 1;
    }
    out << "Installed " << target.fileName() << "\n";
    return 0;
}

} // namespace

bool isCliCommand(const QStringList &arguments) {
    return arguments.size() > 1 && kCommands.contains(arguments.at(1));
}

int runCli(const QStringList &arguments) {
    QTextStream out(stdout), err(stderr);
    const QString command = arguments.at(1);
    const bool json = arguments.contains("--json");
    if (command == "scan")
        return scan(json, out, err);
    if (command == "list")
        return list(json, out);
    if (command == "connect")
        return connectHost(arguments, out, err);
    if (command == "skill" && arguments.value(2) == "install")
        return installSkill(out, err);
    usage(out);
    return command == "skill" ? 1 : 0;
}
