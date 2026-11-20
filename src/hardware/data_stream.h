/**
 * @file data_stream.h
 * @brief Pure function-pointer-driven communication stream (DataStream) decoupling callers from Ethernet/RS232/Mock I/O.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DATA_STREAM_H
#define DATA_STREAM_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QPair>
#include <QVector>
#include <functional>

/**
 * @struct DrxTransportOps
 * @brief Function-pointer / callback dispatch table injected into DataStream (e.g., TCP_send, UART_Send).
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
    std::function<void()> channelListFn;                            ///< Channel list query callback (`channelSelection`)
    std::function<void(int)> selectChannelFn;                       ///< Channel selection callback (`ResponseTo=chnsel`)
};

/**
 * @class DataStream
 * @brief Unified communication stream that delegates physical TX/RX to injected function pointers
 *        with zero dependency on any concrete transport class.
 */
class DataStream : public QObject {
    Q_OBJECT
public:
    using SendFn        = std::function<void(const QByteArray &)>;
    using ConnectFn     = std::function<bool()>;
    using DisconnectFn  = std::function<void()>;
    using IsConnectedFn = std::function<bool()>;
    using TuneFn        = std::function<void(int, int, const QString &)>;
    using StatusFn      = std::function<void()>;

    explicit DataStream(QObject *parent = nullptr);
    explicit DataStream(const DrxTransportOps &ops, QObject *parent = nullptr);
    explicit DataStream(SendFn sendFn,
                        ConnectFn connectFn = nullptr,
                        DisconnectFn disconnectFn = nullptr,
                        IsConnectedFn isConnectedFn = nullptr,
                        TuneFn tuneFn = nullptr,
                        StatusFn statusFn = nullptr,
                        const QString &name = "DataStream",
                        QObject *parent = nullptr);
    ~DataStream() override = default;

    /** @brief Injects a custom function-pointer transport table at runtime. */
    void setTransportOps(const DrxTransportOps &ops);

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
    /** @brief Queries transponder channels via bound transport callback. */
    void requestChannelList();
    /** @brief Selects a channel by index via bound transport callback. */
    void selectChannel(int channelNo);
    /** @brief Returns true if bound transport provides channel list & selection callbacks. */
    bool hasChannelSelectionSupport() const;
    void sendRaw(const QString &command);

public slots:
    void pushStatus(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName = QString());
    void pushTelemetry(double powerDbm, double cnDb, double ber, double ebN0Db = 0.0);
    void pushDeviceInfo(const QString &key, const QString &value);
    void pushTimestamp(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);
    void pushChannelList(const QVector<QPair<int, QString>> &channels);
    void pushChannelSelected(bool ok, const QString &details);
    void pushLog(const QString &message, bool isError);

signals:
    void deviceConnected(const QString &endpoint);
    void deviceDisconnected();
    void statusReceived(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName = QString());
    void telemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db = 0.0);
    void deviceInfoReceived(const QString &key, const QString &value);
    void timestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);
    void channelListReceived(const QVector<QPair<int, QString>> &channels);
    void channelSelected(bool ok, const QString &details);
    void logMessage(const QString &message, bool isError);

private:
    DrxTransportOps m_ops;
};

#endif // DATA_STREAM_H
