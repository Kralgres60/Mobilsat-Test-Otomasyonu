/**
 * @file udp_transport.h
 * @brief Physical QUdpSocket transport implementation.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef UDP_TRANSPORT_H
#define UDP_TRANSPORT_H

#include <QObject>
#include <QUdpSocket>
#include "net_stream.h"

/**
 * @class UdpTransport
 * @brief Physical QUdpSocket transport bound to a NetDatagramStream via NetDatagramOps.
 */
class UdpTransport : public QObject {
    Q_OBJECT
public:
    explicit UdpTransport(QObject *parent = nullptr);
    ~UdpTransport() override;

    /** @brief Returns a NetDatagramOps table bound to this transport. */
    NetDatagramOps ops();

    /** @brief Binds both ops and incoming datagram signals to the target NetDatagramStream. */
    void attachStream(NetDatagramStream *stream);

    bool bindPort(quint16 port);
    void close();
    bool writeBroadcast(const QByteArray &data, quint16 port);

signals:
    void datagramReceived(const QByteArray &data, const QString &senderIp);

private slots:
    void onReadyRead();

private:
    QUdpSocket m_socket;
};

#endif // UDP_TRANSPORT_H
