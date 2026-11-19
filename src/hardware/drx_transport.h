/**
 * @file drx_transport.h
 * @brief Pure virtual hardware transport interface (DataTransfer / IDrxDeviceTransport) and shared telemetry structs.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_TRANSPORT_H
#define DRX_TRANSPORT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>

/**
 * @class DataTransfer
 * @brief Abstract interface class for hardware communication and telemetry.
 *        Unifies frequency tuning, status querying, and telemetry monitoring across Ethernet, RS232, and Mock drivers.
 */
class DataTransfer : public QObject {
    Q_OBJECT
public:
    explicit DataTransfer(QObject *parent = nullptr) : QObject(parent) {}
    ~DataTransfer() override = default;

    /** @brief Returns transport type name ("Ethernet", "RS232", "Mock"). */
    virtual QString transferType() const = 0;

    /** @brief Connects to the device. */
    virtual bool connectDevice() = 0;

    /** @brief Disconnects from the device. */
    virtual void disconnectDevice() = 0;

    /** @brief Checks if the device is connected. */
    virtual bool isConnected() const = 0;

    /** @brief Checks if the device is ready to receive commands. */
    virtual bool isDeviceReady() const { return isConnected(); }

    /** @brief Sends raw byte buffer to hardware (Unified HAL: USB-UART, TCP/Ethernet, or Mock). */
    virtual void sendData(const QByteArray &data) = 0;

    /** @brief Sends frequency tuning command. */
    virtual void sendTune(int freqMhz, int symbolRate, const QString &polarity) = 0;

    /** @brief Requests lock status and live telemetry from device. */
    virtual void requestStatus() = 0;

    /** @brief Requests device info (software version, serial, MAC, etc.). */
    virtual void requestDeviceInfo() {}

    /** @brief Sends raw command string. */
    virtual void sendRaw(const QString &command) = 0;

signals:
    /** @brief Emitted when device connection is established. */
    void deviceConnected(const QString &endpoint);

    /** @brief Emitted when device connection is lost. */
    void deviceDisconnected();

    /** @brief Emitted when lock status is received (signal, video, audio, optional channel name). */
    void statusReceived(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName = QString());

    /** @brief Emitted when telemetry values are received (Power, C/N, BER, optional Eb/N0). */
    void telemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db = 0.0);

    /** @brief Emitted when a device info key-value pair is received. */
    void deviceInfoReceived(const QString &key, const QString &value);

    /** @brief Emitted when telemetry timestamp is captured (device time, host receipt time, raw string). */
    void timestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);

    /** @brief Emitted when a log or error message is produced. */
    void logMessage(const QString &message, bool isError);
};

using IDrxDeviceTransport = DataTransfer;

/**
 * @struct TimestampPair
 * @brief Paired host receipt timestamp and DRX9000 internal clock reading.
 */
struct TimestampPair {
    QDateTime hostTime;         ///< System/PC instant timestamp
    QDateTime deviceTime;       ///< Parsed DRX9000 device timestamp
    QString rawDeviceTime;      ///< Raw time string from DRX (st_current_time)
};

/**
 * @enum DeviceReadyState
 * @brief Device boot and command readiness states on RS232 console.
 */
enum class DeviceReadyState {
    Disconnected,           ///< Serial COM port is closed
    PortOpenWaitingDevice,  ///< Port is open, waiting for device heartbeat
    Booting,                ///< Device is booting / bootloader active
    Ready                   ///< Device is ready (CLI prompt detected)
};

#endif // DRX_TRANSPORT_H
