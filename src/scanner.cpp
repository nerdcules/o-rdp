#include "scanner.h"

#include <QHostInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

Scanner::Scanner(QObject *parent) : QObject(parent) {}

void Scanner::start(const QStringList &addresses) {
    stop();
    ++m_generation;
    if (addresses.isEmpty()) {
        m_subnet = defaultSubnet();
        m_queue = hostAddresses(m_subnet);
    } else {
        m_subnet = Subnet();
        m_queue = addresses;
    }
    m_index = m_done = m_found = m_active = 0;
    m_running = true;
    emit progress(0, int(m_queue.size()));
    if (m_queue.isEmpty()) {
        m_running = false;
        emit finished(0);
        return;
    }
    for (int i = 0; i < parallel && m_index < m_queue.size(); ++i)
        next();
}

void Scanner::stop() {
    ++m_generation;
    m_running = false;
}

void Scanner::next() {
    if (!m_running || m_index >= m_queue.size())
        return;
    probe(m_queue.at(m_index++));
}

void Scanner::probe(const QString &address) {
    ++m_active;
    const int generation = m_generation;
    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    // One outcome per address: connected, failed or timed out.
    auto done = [this, socket, address, generation](bool open) {
        if (socket->property("done").toBool())
            return;
        socket->setProperty("done", true);
        socket->abort();
        socket->deleteLater();
        if (generation != m_generation)
            return;
        --m_active;
        ++m_done;
        if (open) {
            ++m_found;
            emit found(address);
            name(address);
        }
        emit progress(m_done, int(m_queue.size()));
        if (m_index < m_queue.size())
            next();
        else if (m_active == 0 && m_running) {
            m_running = false;
            emit finished(m_found);
        }
    };
    connect(socket, &QTcpSocket::connected, this, [done] { done(true); });
    connect(socket, &QTcpSocket::errorOccurred, this, [done] { done(false); });
    connect(timer, &QTimer::timeout, this, [done] { done(false); });
    timer->start(timeoutMs);
    socket->connectToHost(address, quint16(port));
}

void Scanner::name(const QString &address) {
    // NetBIOS first: a Windows PC answers with its own computer name.
    auto *udp = new QUdpSocket(this);
    auto *timer = new QTimer(udp);
    timer->setSingleShot(true);
    QPointer<QUdpSocket> guard(udp);
    connect(udp, &QUdpSocket::readyRead, this, [this, udp, address] {
        while (udp->hasPendingDatagrams()) {
            QByteArray answer(int(udp->pendingDatagramSize()), 0);
            udp->readDatagram(answer.data(), answer.size());
            const QString found = netbiosName(answer);
            if (!found.isEmpty()) {
                udp->setProperty("named", true);
                emit named(address, found);
                udp->deleteLater();
                return;
            }
        }
    });
    connect(timer, &QTimer::timeout, this, [this, guard, address] {
        if (!guard || guard->property("named").toBool())
            return;
        guard->deleteLater();
        nameByDns(address);
    });
    udp->writeDatagram(netbiosQuery(), QHostAddress(address), 137);
    timer->start(1500);
}

void Scanner::nameByDns(const QString &address) {
    QHostInfo::lookupHost(address, this, [this, address](const QHostInfo &info) {
        QString host = info.error() == QHostInfo::NoError ? info.hostName() : QString();
        if (host.endsWith('.'))
            host.chop(1);
        if (!host.isEmpty() && host != address)
            emit named(address, host);
        else
            nameByAvahi(address);
    });
}

void Scanner::nameByAvahi(const QString &address) {
    const QString avahi = QStandardPaths::findExecutable("avahi-resolve");
    if (avahi.isEmpty())
        return;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, address] {
        const QString host = QString::fromUtf8(process->readAllStandardOutput()).section('\t', 1).trimmed();
        if (!host.isEmpty())
            emit named(address, host);
        process->deleteLater();
    });
    QTimer::singleShot(1500, process, [process] { process->kill(); });
    process->start(avahi, {"-a", address});
}
