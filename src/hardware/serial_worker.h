/**
 * @file serial_worker.h
 * @brief RS232 Serial Port Worker managing low-level UART communication with DRX9000 receivers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SERIAL_WORKER_H
#define SERIAL_WORKER_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "protocolhandler.h"

/**
 * @enum DeviceReadyState
 * @brief Discrete boot and readiness states of the receiver hardware over serial console.
 */
enum class DeviceReadyState {
    Disconnected,           ///< Serial COM port closed
    PortOpenWaitingDevice,  ///< Port open, waiting for initial device response
    Booting,                ///< Receiver booting / bootloader prompt detected
    Ready                   ///< Receiver fully booted and ready (interactive CLI prompt detected)
};

/**
 * @class SerialWorker
 * @brief Manages asynchronous serial communication, frame framing, and hardware handshake with DRX9000.
 */
class SerialWorker : public QObject {
    Q_OBJECT
public:
    explicit SerialWorker(QObject *parent = nullptr);
    ~SerialWorker() override;

    bool openPort(const QString &portName, int baudRate = 115200);
    void closePort();
    bool isConnected() const;
    QString currentPortName() const { return m_serial.portName(); }

    DeviceReadyState readyState() const { return m_readyState; }
    bool isDeviceReady() const { return m_readyState == DeviceReadyState::Ready; }
    QString readyStateString() const;

    QString deviceSwVersion() const { return m_swVer; }
    QString deviceHwVersion() const { return m_hwVer; }
    QString deviceModel() const { return m_model; }

    static QStringList availablePorts();

public slots:
    void sendTune(int freqMHz, int symbolRate, const QString &polarity);
    void requestStatus();
    void startTunerLog();
    void stopTunerLog();
    void sendRaw(const QString &command);

signals:
    void portOpened(const QString &portName);
    void portClosed();
    void portError(const QString &errorString);

    // Boot and Readiness Lifecycle Signals
    void deviceReadyStateChanged(DeviceReadyState state, const QString &description);
    void deviceInfoReceived(const QString &key, const QString &value);
    void devicePromptDetected(const QString &promptLine);

    // Telemetry and Status Signals
    void statusReceived(bool signalLocked, bool videoProducing, bool audioProducing);
    void telemetryReceived(double powerDbm, double cnDb, double ber);
    void logMessage(const QString &msg, bool isTx);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

    void onDeviceInfoParsed(const QString &key, const QString &value);
    void onInteractivePromptDetected(const QString &promptLine);
    void onStatusParsed(bool signalLocked, bool videoProducing, bool audioProducing);

private:
    void setReadyState(DeviceReadyState newState, const QString &desc);

    QSerialPort m_serial;
    ProtocolHandler m_protocol;
    QByteArray m_rxBuffer;

    DeviceReadyState m_readyState = DeviceReadyState::Disconnected;
    QString m_swVer = "Unknown";
    QString m_hwVer = "Unknown";
    QString m_model = "NETA DRX9000";
};

#endif // SERIAL_WORKER_H
