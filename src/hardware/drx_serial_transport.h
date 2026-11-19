/**
 * @file drx_serial_transport.h
 * @brief RS232 UART serial communication driver inheriting DataTransfer.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_SERIAL_TRANSPORT_H
#define DRX_SERIAL_TRANSPORT_H

#include "drx_transport.h"
#include "drx_protocol_config.h"
#include <QStringList>
#include <QSerialPort>
#include <QSerialPortInfo>

/**
 * @class RS232
 * @brief Driver for RS232 serial communication and line-based protocol parsing.
 */
class RS232 : public DataTransfer {
    Q_OBJECT
public:
    explicit RS232(QObject *parent = nullptr);
    explicit RS232(const QString &portName, int baudRate = 115200, QObject *parent = nullptr);
    ~RS232() override;

    QString transferType() const override { return "RS232"; }

    bool connectDevice() override;
    void disconnectDevice() override;
    bool isConnected() const override;
    bool isDeviceReady() const override { return m_readyState == DeviceReadyState::Ready; }

    void sendData(const QByteArray &data) override;
    void sendTune(int freqMhz, int symbolRate, const QString &polarity) override;
    void requestStatus() override;
    void requestDeviceInfo() override;
    void sendRaw(const QString &command) override;

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

#endif // DRX_SERIAL_TRANSPORT_H
