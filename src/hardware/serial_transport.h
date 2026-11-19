/**
 * @file serial_transport.h
 * @brief Physical RS232 UART serial communication transport driver.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SERIAL_TRANSPORT_H
#define SERIAL_TRANSPORT_H

#include "drx_protocol_config.h"
#include <QObject>
#include <QDateTime>
#include <QStringList>
#include <QSerialPort>
#include <QSerialPortInfo>

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

/**
 * @class RS232
 * @brief Physical RS232 UART serial communication driver.
 */
class RS232 : public QObject {
    Q_OBJECT
public:
    explicit RS232(QObject *parent = nullptr);
    explicit RS232(const QString &portName, int baudRate = 115200, QObject *parent = nullptr);
    ~RS232() override;

    QString transferType() const { return "RS232"; }

    bool connectDevice();
    void disconnectDevice();
    bool isConnected() const;
    bool isDeviceReady() const { return m_readyState == DeviceReadyState::Ready; }

    void sendData(const QByteArray &data);
    void sendTune(int freqMhz, int symbolRate, const QString &polarity);
    void requestStatus();
    void requestDeviceInfo();
    void sendRaw(const QString &command);

    bool openPort(const QString &portName, int baudRate = 115200);
    void closePort();
    void setPortConfig(const QString &portName, int baudRate = 115200);

    QString portName() const { return m_portName; }
    QString currentPortName() const { return m_serial.portName(); }
    int baudRate() const { return m_baudRate; }

    DeviceReadyState readyState() const { return m_readyState; }
    QString readyStateString() const;

    QString deviceSwVersion() const { return m_swVer; }
    QString deviceHwVersion() const { return m_hwVer; }
    QString deviceModel() const { return m_model; }

    static QStringList availablePorts();

public slots:
    void startTunerLog();
    void stopTunerLog();

signals:
    void deviceConnected(const QString &endpoint);
    void deviceDisconnected();
    void statusReceived(bool signalLocked, bool videoLocked, bool audioLocked, const QString &channelName = QString());
    void telemetryReceived(double powerDbm, double cnDb, double ber, double ebN0Db = 0.0);
    void deviceInfoReceived(const QString &key, const QString &value);
    void timestampReceived(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);
    void logMessage(const QString &message, bool isError);

    void portOpened(const QString &portName);
    void portClosed();
    void portError(const QString &errorMessage);
    void deviceReadyStateChanged(DeviceReadyState newState, const QString &description);
    void devicePromptDetected(const QString &promptLine);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    void setReadyState(DeviceReadyState newState, const QString &desc);
    void processRxBuffer();
    bool parseRfTelemetryLine(const QString &line, double &outPow, double &outCn, double &outBer);
    QString extractValueAfterKey(const QString &line, const QString &key);

    QSerialPort m_serial;
    QString m_portName = "COM1";
    int m_baudRate = 115200;
    DeviceReadyState m_readyState = DeviceReadyState::Disconnected;
    QByteArray m_rxBuffer;
    QString m_pendingInfoKey;

    QString m_swVer;
    QString m_hwVer;
    QString m_model;
};

using Rs232Transfer = RS232;
using SerialWorker = RS232;

#endif // SERIAL_TRANSPORT_H
