/**
 * @file tcp_transport.cpp
 * @brief Implementation of physical QTcpSocket and QTcpServer transports.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "tcp_transport.h"
#include <QHostAddress>

namespace {

void sc_connect(void *ctx, const QString &host, quint16 port) {
    static_cast<TcpSocketTransport *>(ctx)->connectToHost(host, port);
}
void sc_disconnect(void *ctx) {
    static_cast<TcpSocketTransport *>(ctx)->disconnectFromHost();
}
bool sc_isConnected(void *ctx) {
    return static_cast<TcpSocketTransport *>(ctx)->isConnected();
}
bool sc_write(void *ctx, const QByteArray &data) {
    return static_cast<TcpSocketTransport *>(ctx)->write(data);
}
QString sc_error(void *ctx) {
    return static_cast<TcpSocketTransport *>(ctx)->errorString();
}

bool ss_start(void *ctx, quint16 port) {
    return static_cast<TcpServerTransport *>(ctx)->start(port);
}
void ss_stop(void *ctx) {
    static_cast<TcpServerTransport *>(ctx)->stop();
}
bool ss_isListening(void *ctx) {
    return static_cast<TcpServerTransport *>(ctx)->isListening();
}
bool ss_writeToPeer(void *ctx, quint64 peerId, const QByteArray &data) {
    return static_cast<TcpServerTransport *>(ctx)->writeToPeer(peerId, data);
}
void ss_disconnectPeer(void *ctx, quint64 peerId, bool abortImmediately) {
    static_cast<TcpServerTransport *>(ctx)->disconnectPeer(peerId, abortImmediately);
}
QString ss_error(void *ctx) {
    return static_cast<TcpServerTransport *>(ctx)->errorString();
}

} // namespace

// --- TcpSocketTransport ---

TcpSocketTransport::TcpSocketTransport(QObject *parent) : QObject(parent) {
    connect(&m_socket, &QTcpSocket::connected, this, &TcpSocketTransport::connected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &TcpSocketTransport::disconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
        emit bytesReceived(m_socket.readAll());
    });
    connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(m_socket.errorString());
    });
}

TcpSocketTransport::~TcpSocketTransport() {
    disconnectFromHost();
}

NetClientOps TcpSocketTransport::ops() {
    NetClientOps t;
    t.ctx               = this;
    t.connectToHost     = &sc_connect;
    t.disconnectFromHost= &sc_disconnect;
    t.isConnected       = &sc_isConnected;
    t.write             = &sc_write;
    t.errorString       = &sc_error;
    return t;
}

void TcpSocketTransport::attachStream(NetClientStream *stream) {
    if (!stream) return;
    stream->bind(ops());
    connect(this, &TcpSocketTransport::connected, stream, &NetClientStream::connected);
    connect(this, &TcpSocketTransport::disconnected, stream, &NetClientStream::disconnected);
    connect(this, &TcpSocketTransport::bytesReceived, stream, &NetClientStream::dataReceived);
    connect(this, &TcpSocketTransport::errorOccurred, stream, &NetClientStream::errorOccurred);
}

void TcpSocketTransport::connectToHost(const QString &host, quint16 port) {
    if (m_socket.isOpen()) {
        m_socket.abort();
    }
    m_socket.connectToHost(host, port);
}

void TcpSocketTransport::disconnectFromHost() {
    if (m_socket.isOpen()) {
        m_socket.disconnectFromHost();
    }
}

bool TcpSocketTransport::isConnected() const {
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

bool TcpSocketTransport::write(const QByteArray &data) {
    if (!isConnected()) return false;
    return m_socket.write(data) >= 0;
}

// --- TcpServerTransport ---

TcpServerTransport::TcpServerTransport(QObject *parent) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, &TcpServerTransport::onNewConnection);
}

TcpServerTransport::~TcpServerTransport() {
    stop();
}

NetServerOps TcpServerTransport::ops() {
    NetServerOps t;
    t.ctx            = this;
    t.start          = &ss_start;
    t.stop           = &ss_stop;
    t.isListening    = &ss_isListening;
    t.writeToPeer    = &ss_writeToPeer;
    t.disconnectPeer = &ss_disconnectPeer;
    t.errorString    = &ss_error;
    return t;
}

void TcpServerTransport::attachStream(NetServerStream *stream) {
    if (!stream) return;
    stream->bind(ops());
    connect(this, &TcpServerTransport::peerConnected, stream, &NetServerStream::peerConnected);
    connect(this, &TcpServerTransport::peerDisconnected, stream, &NetServerStream::peerDisconnected);
    connect(this, &TcpServerTransport::bytesReceived, stream, &NetServerStream::dataReceived);
}

bool TcpServerTransport::start(quint16 port) {
    return m_server.listen(QHostAddress::Any, port);
}

void TcpServerTransport::stop() {
    const auto sockets = m_peersById.values();
    for (QTcpSocket *sock : sockets) {
        if (sock) {
            sock->flush();
            sock->disconnectFromHost();
            if (sock->state() != QAbstractSocket::UnconnectedState) {
                sock->close();
            }
        }
    }
    m_peersById.clear();
    m_idBySocket.clear();
    m_server.close();
}

bool TcpServerTransport::writeToPeer(quint64 peerId, const QByteArray &data) {
    QTcpSocket *sock = m_peersById.value(peerId, nullptr);
    if (sock && sock->isOpen()) {
        return sock->write(data) >= 0;
    }
    return false;
}

void TcpServerTransport::disconnectPeer(quint64 peerId, bool abortImmediately) {
    QTcpSocket *sock = m_peersById.value(peerId, nullptr);
    if (!sock) return;
    if (abortImmediately) {
        sock->abort();
    } else {
        sock->disconnectFromHost();
    }
}

void TcpServerTransport::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        const quint64 peerId = m_nextPeerId++;
        QString ip = socket->peerAddress().toString();
        if (ip.startsWith(QStringLiteral("::ffff:"))) {
            ip = ip.mid(7);
        }

        m_peersById.insert(peerId, socket);
        m_idBySocket.insert(socket, peerId);

        connect(socket, &QTcpSocket::readyRead, this, &TcpServerTransport::onSocketReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &TcpServerTransport::onSocketDisconnected);

        emit peerConnected(peerId, ip);
    }
}

void TcpServerTransport::onSocketReadyRead() {
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_idBySocket.contains(socket)) return;
    const quint64 peerId = m_idBySocket.value(socket);
    emit bytesReceived(peerId, socket->readAll());
}

void TcpServerTransport::onSocketDisconnected() {
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_idBySocket.contains(socket)) return;
    const quint64 peerId = m_idBySocket.take(socket);
    m_peersById.remove(peerId);
    emit peerDisconnected(peerId);
    socket->deleteLater();
}
