/**
 * @file udp_transport.cpp
 * @brief Implementation of the physical QUdpSocket transport.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "udp_transport.h"
#include <QHostAddress>
#include <QNetworkDatagram>

namespace {

bool su_bind(void *ctx, quint16 port) {
    return static_cast<UdpTransport *>(ctx)->bindPort(port);
}
void su_close(void *ctx) {
    static_cast<UdpTransport *>(ctx)->close();
}
bool su_writeBroadcast(void *ctx, const QByteArray &data, quint16 port) {
    return static_cast<UdpTransport *>(ctx)->writeBroadcast(data, port);
}

} // namespace

UdpTransport::UdpTransport(QObject *parent) : QObject(parent) {
    connect(&m_socket, &QUdpSocket::readyRead, this, &UdpTransport::onReadyRead);
}

UdpTransport::~UdpTransport() {
    close();
}

NetDatagramOps UdpTransport::ops() {
    NetDatagramOps t;
    t.ctx            = this;
    t.bind           = &su_bind;
    t.close          = &su_close;
    t.writeBroadcast = &su_writeBroadcast;
    return t;
}

void UdpTransport::attachStream(NetDatagramStream *stream) {
    if (!stream) return;
    stream->bind(ops());
    connect(this, &UdpTransport::datagramReceived, stream, &NetDatagramStream::datagramReceived);
}

bool UdpTransport::bindPort(quint16 port) {
    return m_socket.bind(port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
}

void UdpTransport::close() {
    m_socket.close();
}

bool UdpTransport::writeBroadcast(const QByteArray &data, quint16 port) {
    const qint64 written = m_socket.writeDatagram(data, QHostAddress::Broadcast, port);
    m_socket.flush();
    return written >= 0;
}

void UdpTransport::onReadyRead() {
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        QString senderIp = datagram.senderAddress().toString();
        if (senderIp.startsWith(QStringLiteral("::ffff:"))) {
            senderIp = senderIp.mid(7);
        }
        emit datagramReceived(datagram.data(), senderIp);
    }
}
