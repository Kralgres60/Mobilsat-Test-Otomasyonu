/**
 * @file drx_data_control.h
 * @brief Unified function-pointer-driven communication controller (DrxDataControl) decoupling protocol flow from TCP/UART/Mock I/O.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_DATA_CONTROL_H
#define DRX_DATA_CONTROL_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QMetaObject>
#include <functional>

class Ethernet;
class RS232;

/**
 * @struct DrxTransportOps
 * @brief Function-pointer / callback dispatch table injected into DrxDataControl (e.g., TCP_send, UART_Send).
 */
struct DrxTransportOps {
    QString name = "Unbound";                                       ///< Human-readable transport label
    std::function<bool()> connectFn;                                ///< Connect / open physical channel
    std::function<void()> disconnectFn;                             ///< Disconnect / close physical channel
    std::function<bool()> isConnectedFn;                            ///< Returns true if physical channel is open
    std::function<bool()> isReadyFn;                                ///< Returns true if device is ready for commands
    std::function<void(const QByteArray &)> sendFn;                 ///< Raw TX callback (TCP_send / UART_Send)
    std::function<void(int, int, const QString &)> tuneFn;          ///< Transponder tune callback
    std::function<void()> statusFn;                                 ///< Status / telemetry query callback
    std::function<void()> deviceInfoFn;                             ///< Device info query callback
};

/**
 * @class DrxDataControl
 * @brief Single concrete communication controller that delegates physical TX/RX to injected function pointers.
 */
class DrxDataControl : public QObject {
    Q_OBJECT
public:
    explicit DrxDataControl(QObject *parent = nullptr);
    explicit DrxDataControl(const DrxTransportOps &ops, QObject *parent = nullptr);
    ~DrxDataControl() override = default;

    /** @brief Injects a custom function-pointer transport table at runtime. */
    void setTransportOps(const DrxTransportOps &ops);

    /** @brief Binds an Ethernet (Web CGI) instance via function pointers. */
    void bindEthernet(Ethernet *eth);

    /** @brief Binds an RS232 (Serial UART) instance via function pointers. */
    void bindSerial(RS232 *serial);

    /** @brief Binds an in-process deterministic mock transport via function pointers. */
    void bindMock(double powerDbm = -42.0, double cnDb = 14.5, double ber = 1e-7, bool locked = true);

    /** @brief Returns active transport label ("Ethernet", "RS232", "Mock", etc.). */
    QString transportName() const { return m_ops.name; }

    bool connectDevice();
    void disconnectDevice();
    bool isConnected() const;
    bool isDeviceReady() const;

    void sendData(const QByteArray &data);
    void sendTune(int freqMhz, int symbolRate, const QString &polarity);
    void requestStatus();
    void requestDeviceInfo();
    void sendRaw(const QString &command);

public slots:
    /** @brief Feeds parsed lock status from the active physical driver into unified signals. */
    void pushStatus(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName = QString());

    /** @brief Feeds parsed RF telemetry from the active physical driver into unified signals. */
    void pushTelemetry(double powerDbm, double cnDb, double ber, double ebN0Db = 0.0);

    /** @brief Feeds device info key-value pairs into unified signals. */
    void pushDeviceInfo(const QString &key, const QString &value);

    /** @brief Feeds paired timestamps into unified signals. */
    void pushTimestamp(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);

    /** @brief Feeds diagnostic log messages into unified signals. */
    void pushLog(const QString &message, bool isError);

signals:
    void deviceConnected(const QString &endpoint);
    void deviceDisconnected();
    void statusReceived(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName = QString());
    void telemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db = 0.0);
    void deviceInfoReceived(const QString &key, const QString &value);
    void timestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);
    void logMessage(const QString &message, bool isError);

private:
    void clearBoundConnections();

    DrxTransportOps m_ops;
    QVector<QMetaObject::Connection> m_boundConnections;
};

#endif // DRX_DATA_CONTROL_H
