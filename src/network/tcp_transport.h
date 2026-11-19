/**
 * @file tcp_transport.h
 * @brief Physical QTcpSocket and QTcpServer transport implementations.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TCP_TRANSPORT_H
#define TCP_TRANSPORT_H

#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include "net_stream.h"

/**
 * @class TcpSocketTransport
 * @brief Physical QTcpSocket transport bound to a NetClientStream via NetClientOps.
 */
class TcpSocketTransport : public QObject {
    Q_OBJECT
public:
    explicit TcpSocketTransport(QObject *parent = nullptr);
    ~TcpSocketTransport() override;

    /** @brief Returns a NetClientOps table bound to this transport. */
    NetClientOps ops();

    /** @brief Binds both ops and incoming socket signals to the target NetClientStream. */
    void attachStream(NetClientStream *stream);

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    bool write(const QByteArray &data);
    QString errorString() const { return m_socket.errorString(); }

signals:
    void connected();
    void disconnected();
    void bytesReceived(const QByteArray &data);
    void errorOccurred(const QString &error);

private:
    QTcpSocket m_socket;
};

/**
 * @class TcpServerTransport
 * @brief Physical QTcpServer transport bound to a NetServerStream via NetServerOps.
 */
class TcpServerTransport : public QObject {
    Q_OBJECT
public:
    explicit TcpServerTransport(QObject *parent = nullptr);
    ~TcpServerTransport() override;

    /** @brief Returns a NetServerOps table bound to this transport. */
    NetServerOps ops();

    /** @brief Binds both ops and incoming connection/data signals to the target NetServerStream. */
    void attachStream(NetServerStream *stream);

    bool start(quint16 port);
    void stop();
    bool isListening() const { return m_server.isListening(); }
    bool writeToPeer(quint64 peerId, const QByteArray &data);
    void disconnectPeer(quint64 peerId, bool abortImmediately = false);
    QString errorString() const { return m_server.errorString(); }

signals:
    void peerConnected(quint64 peerId, const QString &ipAddress);
    void peerDisconnected(quint64 peerId);
    void bytesReceived(quint64 peerId, const QByteArray &data);

private slots:
    void onNewConnection();
    void onSocketReadyRead();
    void onSocketDisconnected();

private:
    QTcpServer m_server;
    quint64 m_nextPeerId = 1;
    QMap<quint64, QTcpSocket*> m_peersById;
    QMap<QTcpSocket*, quint64> m_idBySocket;
};

#endif // TCP_TRANSPORT_H
