#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "hosts.h"

class QTcpSocket;
class QUdpSocket;

// Finds the PCs on the local subnet with Remote Desktop's port open, the way
// rdp-connect does: a TCP connect to 3389 on every address (256 at a time,
// half a second each), then a name for each one found (NetBIOS, then DNS,
// then Avahi).
class Scanner : public QObject {
    Q_OBJECT

public:
    explicit Scanner(QObject *parent = nullptr);

    // addresses defaults to every host on the default route's subnet.
    void start(const QStringList &addresses = {});
    void stop();
    bool running() const { return m_running; }
    int probed() const { return m_done; }
    int total() const { return int(m_queue.size()); }
    Subnet subnet() const { return m_subnet; }

    int port = 3389;
    int timeoutMs = 500;
    int parallel = 256;

signals:
    void found(const QString &address);
    void named(const QString &address, const QString &name);
    void progress(int probed, int total);
    void finished(int found);

private:
    void next();
    void probe(const QString &address);
    void name(const QString &address);
    void nameByDns(const QString &address);
    void nameByAvahi(const QString &address);

    Subnet m_subnet;
    QStringList m_queue;
    int m_index = 0;
    int m_active = 0;
    int m_done = 0;
    int m_found = 0;
    bool m_running = false;
    int m_generation = 0;
};
