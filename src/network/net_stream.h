/**
 * @file net_stream.h
 * @brief Transport-agnostic network abstraction layer using function pointers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef NET_STREAM_H
#define NET_STREAM_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>

/**
 * @struct NetClientOps
 * @brief Function-pointer operation table decoupling client protocol logic from concrete sockets.
 */
struct NetClientOps {
    void *ctx = nullptr;
    void    (*connectToHost)(void *ctx, const QString &host, quint16 port) = nullptr;
    void    (*disconnectFromHost)(void *ctx) = nullptr;
    bool    (*isConnected)(void *ctx) = nullptr;
    bool    (*write)(void *ctx, const QByteArray &data) = nullptr;
    QString (*errorString)(void *ctx) = nullptr;
};

/**
 * @struct NetServerOps
 * @brief Function-pointer operation table decoupling server protocol logic from concrete listeners.
 */
struct NetServerOps {
    void *ctx = nullptr;
    bool    (*start)(void *ctx, quint16 port) = nullptr;
    void    (*stop)(void *ctx) = nullptr;
    bool    (*isListening)(void *ctx) = nullptr;
    bool    (*writeToPeer)(void *ctx, quint64 peerId, const QByteArray &data) = nullptr;
    void    (*disconnectPeer)(void *ctx, quint64 peerId, bool abortImmediately) = nullptr;
    QString (*errorString)(void *ctx) = nullptr;
};

/**
 * @struct NetDatagramOps
 * @brief Function-pointer operation table decoupling datagram/discovery logic from QUdpSocket.
 */
struct NetDatagramOps {
    void *ctx = nullptr;
    bool (*bind)(void *ctx, quint16 port) = nullptr;
    void (*close)(void *ctx) = nullptr;
    bool (*writeBroadcast)(void *ctx, const QByteArray &data, quint16 port) = nullptr;
};

/**
 * @class NetClientStream
 * @brief Transport-agnostic client byte stream dispatching via bound NetClientOps.
 */
class NetClientStream : public QObject {
    Q_OBJECT
public:
    explicit NetClientStream(QObject *parent = nullptr);
    explicit NetClientStream(const NetClientOps &ops, QObject *parent = nullptr);
    ~NetClientStream() override = default;

    /** @brief Binds underlying client transport operations. */
    void bind(const NetClientOps &ops);

    /** @brief Returns true if transport operations are bound. */
    bool isBound() const { return m_ops.connectToHost != nullptr && m_ops.write != nullptr; }

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    bool write(const QByteArray &data);
    QString errorString() const;

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);

private:
    NetClientOps m_ops;
};

/**
 * @class NetServerStream
 * @brief Transport-agnostic server byte stream dispatching via bound NetServerOps.
 */
class NetServerStream : public QObject {
    Q_OBJECT
public:
    explicit NetServerStream(QObject *parent = nullptr);
    explicit NetServerStream(const NetServerOps &ops, QObject *parent = nullptr);
    ~NetServerStream() override = default;

    /** @brief Binds underlying server transport operations. */
    void bind(const NetServerOps &ops);

    /** @brief Returns true if server transport operations are bound. */
    bool isBound() const { return m_ops.start != nullptr && m_ops.writeToPeer != nullptr; }

    bool start(quint16 port);
    void stop();
    bool isListening() const;
    bool writeToPeer(quint64 peerId, const QByteArray &data);
    void disconnectPeer(quint64 peerId, bool abortImmediately = false);
    QString errorString() const;

signals:
    void peerConnected(quint64 peerId, const QString &ipAddress);
    void peerDisconnected(quint64 peerId);
    void dataReceived(quint64 peerId, const QByteArray &data);

private:
    NetServerOps m_ops;
};

/**
 * @class NetDatagramStream
 * @brief Transport-agnostic datagram stream dispatching via bound NetDatagramOps.
 */
class NetDatagramStream : public QObject {
    Q_OBJECT
public:
    explicit NetDatagramStream(QObject *parent = nullptr);
    explicit NetDatagramStream(const NetDatagramOps &ops, QObject *parent = nullptr);
    ~NetDatagramStream() override = default;

    /** @brief Binds underlying datagram transport operations. */
    void bind(const NetDatagramOps &ops);

    /** @brief Returns true if datagram transport operations are bound. */
    bool isBound() const { return m_ops.writeBroadcast != nullptr || m_ops.bind != nullptr; }

    bool bindPort(quint16 port);
    void close();
    bool writeBroadcast(const QByteArray &data, quint16 port);

signals:
    void datagramReceived(const QByteArray &data, const QString &senderIp);

private:
    NetDatagramOps m_ops;
};

#endif // NET_STREAM_H
