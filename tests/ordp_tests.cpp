#include <QtTest>

#include <QTcpServer>

#include "backend.h"
#include "hosts.h"
#include "keyring.h"
#include "scanner.h"

class OrdpTests : public QObject {
    Q_OBJECT

private slots:
    void addressesOfASubnet() {
        Subnet s24{"wlan0", "192.168.8.213", 24};
        const QStringList a = hostAddresses(s24);
        QCOMPARE(a.size(), 254);
        QCOMPARE(a.first(), QString("192.168.8.1"));
        QCOMPARE(a.last(), QString("192.168.8.254"));
        QCOMPARE(s24.cidr(), QString("192.168.8.0/24"));
        Subnet s22{"wlan0", "192.168.9.7", 22};
        QCOMPARE(hostAddresses(s22).size(), 1022);
        QCOMPARE(hostAddresses(s22).first(), QString("192.168.8.1"));
        QCOMPARE(s22.cidr(), QString("192.168.8.0/22"));
        // Bigger than a /20: the /24 around this computer, as rdp-connect.
        Subnet s16{"eth0", "10.20.30.40", 16};
        const QStringList b = hostAddresses(s16);
        QCOMPARE(b.size(), 254);
        QCOMPARE(b.first(), QString("10.20.30.1"));
        QVERIFY(hostAddresses(Subnet()).isEmpty());
    }

    void defaultRouteWins() {
        const QString table =
            "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
            "tailscale0\t0000640A\t00000000\t0001\t0\t0\t0\t0000FFFF\t0\t0\t0\n"
            "wlan0\t00000000\t0104A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
            "eth0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n";
        QCOMPARE(defaultRouteDevice(table), QString("eth0")); // lowest metric
        QCOMPARE(defaultRouteDevice("Iface\tDestination\n"), QString());
    }

    void netbiosNameFromAnAnswer() {
        const QByteArray query = netbiosQuery();
        QCOMPARE(query.size(), 50);
        QCOMPARE(query.mid(13, 32), QByteArray("CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
        // An answer: the query's header and name, then a name table with a
        // group name first (skipped) and the workstation name.
        QByteArray answer = query.left(46);
        answer += QByteArray::fromHex("00210001");
        answer += QByteArray::fromHex("00000000"); // TTL
        answer += QByteArray::fromHex("0041");     // length
        answer += char(2);                          // names
        answer += QByteArray("WORKGROUP      ") + char(0) + QByteArray::fromHex("8400");
        answer += QByteArray("DESKTOP-ABC    ") + char(0) + QByteArray::fromHex("0400");
        QCOMPARE(answer.at(56), char(2));
        QCOMPARE(netbiosName(answer), QString("DESKTOP-ABC"));
        QCOMPARE(netbiosName(answer.left(60)), QString());
    }

    void knownHostsShareTheScriptsFile() {
        QTemporaryDir directory;
        const QString path = directory.filePath("rdp-connect/known");
        QDir().mkpath(directory.filePath("rdp-connect"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("192.0.2.44\tDESKTOP-TEST\talex\nbox.example.net\t\talan\n");
        file.close();
        KnownHosts known(path);
        QCOMPARE(known.all().size(), 2);
        QCOMPARE(known.find("192.0.2.44").name, QString("DESKTOP-TEST"));
        // An empty field keeps what was there; a new host goes last.
        QVERIFY(known.remember("192.0.2.44", QString(), "admin"));
        QVERIFY(known.remember("192.168.8.9", "LAPTOP", QString()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()),
                 QString("192.0.2.44\tDESKTOP-TEST\tadmin\nbox.example.net\t\talan\n192.168.8.9\tLAPTOP\t\n"));
        file.close();
        QVERIFY(known.forget("box.example.net"));
        QCOMPARE(known.all().size(), 2);
        QVERIFY(!known.forget("nope"));
    }

    void freerdpGetsTheScriptsDefaults() {
        ConnectOptions options{"192.0.2.44", "alex", "s3cret", true, true, "/gfx:avc444 /scale:140"};
        const QStringList args = freerdpArguments(options);
        QCOMPARE(args.mid(0, 8), (QStringList{"/v:192.0.2.44", "/u:alex", "/dynamic-resolution", "/cert:tofu",
                                             "/sound", "/clipboard", "/network:lan", "+auto-reconnect"}));
        QVERIFY(args.contains("/p:s3cret"));
        QVERIFY(args.contains("/f"));
        QVERIFY(args.contains("/multimon"));
        QVERIFY(args.contains("/gfx:avc444"));
        QVERIFY(args.contains("/scale:140"));
        options.password.clear();
        const QStringList without = freerdpArguments(options);
        QVERIFY(std::none_of(without.begin(), without.end(), [](const QString &a) { return a.startsWith("/p:"); }));
    }

    void problemsInPlainWords() {
        QVERIFY(connectionProblem(0, "anything").isEmpty());
        QVERIFY(connectionProblem(1, "").isEmpty()); // the PC ended the session
        QVERIFY(connectionProblem(131, "[ERROR][com.freerdp.core] - [nla_recv_pdu]: ERRCONNECT_LOGON_FAILURE [0x00020014]")
                    .contains("user name or password"));
        QVERIFY(connectionProblem(131, "connect to 192.168.8.9:3389 failed with error: Connection refused [111]")
                    .contains("port 3389"));
        QVERIFY(connectionProblem(147, "[ERROR][x] - [y]: something odd happened").contains("something odd happened"));
        QVERIFY(connectionProblem(147, "").contains("147"));
    }

    void keyringKeepsPasswordsOffTheCommandLine() {
        QList<QStringList> calls;
        QList<QByteArray> inputs;
        Keyring keyring([&](const QStringList &arguments, const QByteArray &input) {
            calls.append(arguments);
            inputs.append(input);
            return std::pair<int, QString>(0, arguments.first() == "lookup" ? "s3cret" : "");
        });
        QVERIFY(keyring.store("192.0.2.44", "alex", "s3cret"));
        QCOMPARE(calls.last().first(), QString("store"));
        QVERIFY(!calls.last().join(' ').contains("s3cret"));
        QCOMPARE(inputs.last(), QByteArray("s3cret"));
        QCOMPARE(keyring.lookup("192.0.2.44", "alex"), QString("s3cret"));
        QCOMPARE(calls.last(), (QStringList{"lookup", "service", "o-rdp", "host", "192.0.2.44", "user", "alex"}));
    }

    void scannerFindsAnOpenPort() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Scanner scanner;
        scanner.port = server.serverPort();
        scanner.timeoutMs = 300;
        QSignalSpy found(&scanner, &Scanner::found);
        QSignalSpy finished(&scanner, &Scanner::finished);
        // 127.0.0.1 listens; 127.0.0.2 doesn't (loopback refuses at once).
        scanner.start({"127.0.0.1", "127.0.0.2"});
        QVERIFY(finished.wait(5000));
        QCOMPARE(found.count(), 1);
        QCOMPARE(found.first().first().toString(), QString("127.0.0.1"));
        QCOMPARE(finished.first().first().toInt(), 1);
        QVERIFY(!scanner.running());
    }

    void connectSendsArgumentsThroughStdin() {
        // A FreeRDP stand-in that keeps its arguments and its stdin.
        QTemporaryDir directory;
        const QString fake = directory.filePath("xfreerdp3");
        QFile script(fake);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(QString("#!/bin/sh\nprintf '%s\\n' \"$@\" > '%1/argv'\ncat > '%1/stdin'\n"
                             "echo '[ERROR][com.freerdp.core] - [nla]: ERRCONNECT_LOGON_FAILURE' >&2\nexit 131\n")
                         .arg(directory.path())
                         .toUtf8());
        script.close();
        script.setPermissions(script.permissions() | QFileDevice::ExeOwner);
        QList<QStringList> keyringCalls;
        Keyring keyring([&](const QStringList &arguments, const QByteArray &) {
            keyringCalls.append(arguments);
            return std::pair<int, QString>(0, QString());
        });
        Backend backend(directory.filePath("known"), fake, keyring);
        QSignalSpy problem(&backend, &Backend::problemChanged);
        backend.connectTo("192.0.2.44", "alex", "s3cret", false);
        QVERIFY(problem.wait(5000));

        QFile argv(directory.filePath("argv")), input(directory.filePath("stdin"));
        QVERIFY(argv.open(QIODevice::ReadOnly) && input.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(argv.readAll()).trimmed(), QString("/args-from:stdin")); // nothing else
        const QStringList lines = QString::fromUtf8(input.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.first(), QString("/v:192.0.2.44"));
        QVERIFY(lines.contains("/p:s3cret"));
        QVERIFY(backend.lastArguments().contains("/p:***")); // nothing kept in memory in clear
        QVERIFY(backend.problem().value("text").toString().contains("user name or password"));
        // The user is remembered for next time, as rdp-connect does.
        QCOMPARE(KnownHosts(directory.filePath("known")).find("192.0.2.44").user, QString("alex"));
    }
};

QTEST_MAIN(OrdpTests)
#include "ordp_tests.moc"
