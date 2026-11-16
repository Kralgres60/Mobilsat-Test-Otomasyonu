/**
 * @file drx_protocol_config.h
 * @brief Dynamic template and format configuration manager for DRX REST CGI, RS232, and RS485 protocols.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_PROTOCOL_CONFIG_H
#define DRX_PROTOCOL_CONFIG_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QJsonObject>
#include <QSettings>
#include <QRegularExpression>

/**
 * @class DrxProtocolConfig
 * @brief Loads and provides customizable request and response format templates.
 * 
 * Users can edit config/drx_protocols.ini to change payload strings,
 * command templates, or response key names without recompiling C++ source code.
 */
class DrxProtocolConfig : public QObject {
    Q_OBJECT
public:
    /** @brief Returns singleton instance. */
    static DrxProtocolConfig &instance();

    /** @brief Loads or reloads protocol format templates from INI file. */
    void load();

    /** @brief Returns current configuration file path. */
    QString configFilePath() const { return m_filePath; }

    // =========================================================================
    // REST / HTTP CGI FORMAT TEMPLATES
    // =========================================================================
    QString cgiEndpointPath() const { return m_cgiEndpointPath; }
    QString cgiLoginFormat() const { return m_cgiLoginFormat; }
    QString cgiStatusFormat() const { return m_cgiStatusFormat; }
    QString cgiDeviceInfoFormat() const { return m_cgiDeviceInfoFormat; }
    QString cgiTuneFormat() const { return m_cgiTuneFormat; }
    QString cgiRawQueryFormat() const { return m_cgiRawQueryFormat; }

    /** @brief Formats login request payload using template. */
    QByteArray formatCgiLogin(const QString &user, const QString &pass) const;

    /** @brief Formats status/telemetry query request payload using template. */
    QByteArray formatCgiStatus(int sessionKey) const;

    /** @brief Formats device info query request payload using template. */
    QByteArray formatCgiDeviceInfo(int sessionKey) const;

    /** @brief Formats frequency tuning request payload using template. */
    QByteArray formatCgiTune(int sessionKey, int freqKhz, int symbolRate, int polEnum) const;

    /** @brief Formats raw query request payload using template. */
    QByteArray formatCgiRawQuery(const QString &query, int sessionKey) const;

    // REST / JSON Response Extraction Helpers
    int extractSessionKey(const QJsonObject &root) const;
    double extractPower(const QJsonObject &root, double fallback = -999.0) const;
    double extractCn(const QJsonObject &root, double fallback = 0.0) const;
    double extractBer(const QJsonObject &root, double fallback = 1.0e-7) const;
    double extractEbN0(const QJsonObject &root, double fallback = 0.0) const;
    bool extractCarrierLock(const QJsonObject &root) const;
    bool extractVideoLock(const QJsonObject &root) const;
    bool extractAudioLock(const QJsonObject &root) const;
    QString extractChannelName(const QJsonObject &root) const;

    // =========================================================================
    // RS232 / SERIAL / USB-UART FORMAT TEMPLATES
    // =========================================================================
    QString serialStatusCmd() const { return m_serialStatusCmd; }
    QString serialDeviceInfoCmd() const { return m_serialDeviceInfoCmd; }
    QString serialTunerLogOnCmd() const { return m_serialTunerLogOnCmd; }
    QString serialTunerLogOffCmd() const { return m_serialTunerLogOffCmd; }

    /** @brief Formats serial frequency tune command using template. */
    QString formatSerialTune(int freqMhz, int symbolRate, const QString &polarity) const;

    /** @brief Checks if a serial terminal line contains prompt detection tokens. */
    bool isPromptLine(const QString &line) const;

    /** @brief Checks if a serial terminal line is a status response line. */
    bool isStatusResponseLine(const QString &line) const;

    /** @brief Parses carrier, video, audio locks from serial status line. */
    void parseSerialLocks(const QString &line, bool &outCarrier, bool &outVideo, bool &outAudio) const;

    /** @brief Parses RF power, C/N, and BER telemetry from serial log line. */
    bool parseSerialTelemetry(const QString &line, double &outPower, double &outCn, double &outBer) const;

    // =========================================================================
    // RS485 / BUS EXTENSION TEMPLATES
    // =========================================================================
    int rs485NodeAddress() const { return m_rs485NodeAddress; }
    QString rs485FramingMode() const { return m_rs485FramingMode; }
    QString formatRs485StatusCmd() const;
    QString formatRs485TuneCmd(int freqMhz, int symbolRate, const QString &polarity) const;

signals:
    /** @brief Emitted when configuration settings are reloaded. */
    void protocolConfigChanged();

private:
    explicit DrxProtocolConfig(QObject *parent = nullptr);
    ~DrxProtocolConfig() override = default;
    Q_DISABLE_COPY(DrxProtocolConfig)

    void ensureDefaultsFile(const QString &path);
    static QString replaceTokens(QString tmpl, const QMap<QString, QString> &tokens);
    double extractFirstDouble(const QJsonObject &root, const QStringList &keys, double fallback) const;
    bool extractFirstBool(const QJsonObject &root, const QStringList &keys) const;
    QString extractFirstString(const QJsonObject &root, const QStringList &keys) const;

    QString m_filePath;

    // REST CGI Settings
    QString m_cgiEndpointPath = "/cgi-bin/drx.cgi";
    QString m_cgiLoginFormat;
    QString m_cgiStatusFormat;
    QString m_cgiDeviceInfoFormat;
    QString m_cgiTuneFormat;
    QString m_cgiRawQueryFormat;

    QStringList m_cgiSessionKeyFields;
    QStringList m_cgiPowerFields;
    QStringList m_cgiCnFields;
    QStringList m_cgiBerFields;
    QStringList m_cgiEbN0Fields;
    QStringList m_cgiCarrierLockFields;
    QStringList m_cgiVideoLockFields;
    QStringList m_cgiAudioLockFields;
    QStringList m_cgiChannelNameFields;

    // RS232 Settings
    QStringList m_serialPromptTokens;
    QStringList m_serialStatusPrefixes;
    QString m_serialStatusCmd = "getstatus\n";
    QString m_serialDeviceInfoCmd = "getstatus\n";
    QString m_serialTuneFormat = "tune {freqMhz} {srate} {pol}\n";
    QString m_serialTunerLogOnCmd = "tunerlog on\n";
    QString m_serialTunerLogOffCmd = "tunerlog off\n";

    QString m_serialCarrierLockToken = "S:+";
    QString m_serialVideoLockToken = "V:+";
    QString m_serialAudioLockToken = "A:+";
    QRegularExpression m_serialPowerRegex;
    QRegularExpression m_serialCnRegex;
    QRegularExpression m_serialBerRegex;

    // RS485 Settings
    int m_rs485NodeAddress = 1;
    QString m_rs485FramingMode = "ASCII";
    QString m_rs485StatusFormat;
    QString m_rs485TuneFormat;
};

#endif // DRX_PROTOCOL_CONFIG_H
