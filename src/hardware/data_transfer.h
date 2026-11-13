/**
 * @file data_transfer.h
 * @brief Abstract base class for hardware data transfer and derived Ethernet (Web CGI) and RS232 drivers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DATA_TRANSFER_H
#define DATA_TRANSFER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QUrl>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSerialPort>
#include <QSerialPortInfo>

/**
 * @class DataTransfer
 * @brief Abstract interface class for hardware communication and telemetry.
 *        Unifies frequency tuning, status querying, and telemetry monitoring.
 */
class DataTransfer : public QObject {
    Q_OBJECT
public:
    explicit DataTransfer(QObject *parent = nullptr) : QObject(parent) {}
    ~DataTransfer() override = default;

    /** @brief Returns transport type name ("Ethernet" or "RS232"). */
    virtual QString transferType() const = 0;

    /** @brief Connects to the device. */
    virtual bool connectDevice() = 0;

    /** @brief Disconnects from the device. */
    virtual void disconnectDevice() = 0;

    /** @brief Checks if the device is connected. */
    virtual bool isConnected() const = 0;

    /** @brief Checks if the device is ready to receive commands. */
    virtual bool isDeviceReady() const { return isConnected(); }

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

    /** @brief Emitted when a log or error message is produced. */
    void logMessage(const QString &message, bool isError);
};

// =============================================================================
// ETHERNET (WEB CGI + ICMP PING)
// =============================================================================
/**
 * @class Ethernet
 * @brief Full-featured driver using CAT6 Ethernet Web CGI (/cgi-bin/drx.cgi) and ICMP Ping.
 */
class Ethernet : public DataTransfer {
    Q_OBJECT
public:
    explicit Ethernet(QObject *parent = nullptr);
    explicit Ethernet(const QString &hostIp, int port = 80, QObject *parent = nullptr);
    ~Ethernet() override;

    QString transferType() const override { return "Ethernet"; }

    bool connectDevice() override;
    void disconnectDevice() override;
    bool isConnected() const override;
    bool isDeviceReady() const override;

    void sendTune(int freqMhz, int symbolRate, const QString &polarity) override;
    void requestStatus() override;
    void requestDeviceInfo() override;
    void sendRaw(const QString &command) override;

    void setHost(const QString &hostIp, int port = 80);
    QString host() const { return m_hostIp; }
    int port() const { return m_port; }

    void setCredentials(const QString &username, const QString &password);
    QString username() const { return m_username; }
    QString password() const { return m_password; }

    bool isReachable() const { return m_isReachable; }
    int lastPingLatencyMs() const { return m_lastLatencyMs; }
    bool isLoggedIn() const { return m_sessionKey > 0; }
    int sessionKey() const { return m_sessionKey; }

    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    int timeoutMs() const { return m_timeoutMs; }
    void resetSession();

public slots:
    void pingHost(int timeoutMs = 1000);
    void testConnectionAndSync();
    void login();
    void sendRawQuery(const QString &queryString) { sendRaw(queryString); }

signals:
    void pingFinished(bool reachable, int latencyMs, const QString &hostIp);
    void loginSucceeded(int sessionKey);
    void loginFailed(const QString &reason);
    void tuneCompleted(bool isOk, const QString &details);
    void rawResponseReceived(const QString &actionName, const QByteArray &rawJson);

    // Extended signals for DrxWebClient compatibility
    void statusReceivedFull(bool signalLocked, bool videoProducing, bool audioProducing, const QString &channelName);
    void telemetryReceivedFull(double powerDbm, double cnDb, double ber, double ebN0Db);
    void deviceInfoReceivedFull(const QString &swVersion, const QString &deviceId, const QString &mac, const QString &tunerModel);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QUrl cgiUrl() const;
    void sendCgiRequest(const QString &actionType, const QByteArray &postData);
    void parseCgiResponse(const QString &actionType, const QByteArray &payload);

    void handleLoginResponse(const QJsonObject &root);
    void handleAlarmStatusResponse(const QJsonObject &root);
    void handleDeviceInfoResponse(const QJsonObject &root);
    void handleTuneResponse(const QJsonObject &root);

    QNetworkAccessManager m_netManager;
    QString m_hostIp = "192.168.0.167";
    int m_port = 80;
    QString m_username = "admin";
    QString m_password = "1234";
    int m_sessionKey = 0;
    int m_timeoutMs = 4000;

    bool m_isReachable = false;
    int m_lastLatencyMs = -1;
    bool m_pingInProgress = false;
    bool m_syncAfterPing = false;

    QString m_pendingActionAfterLogin;
    QByteArray m_pendingDataAfterLogin;
};

// =============================================================================
// RS232 (SERIAL PORT UART AND PROTOCOL PARSER)
// =============================================================================
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

// =============================================================================
// TYPE ALIASES (100% Backward Compatibility)
// =============================================================================
using EthernetTransfer = Ethernet;
using Rs232Transfer = RS232;
using DrxWebClient = Ethernet;
using SerialWorker = RS232;

#endif // DATA_TRANSFER_H
