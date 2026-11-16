/**
 * @file drx_protocol_config.cpp
 * @brief Dynamic template and format configuration manager implementation.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_protocol_config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

DrxProtocolConfig &DrxProtocolConfig::instance() {
    static DrxProtocolConfig s_inst;
    return s_inst;
}

DrxProtocolConfig::DrxProtocolConfig(QObject *parent)
    : QObject(parent) {
    load();
}

static QStringList parseIniStringList(const QVariant &var, const QString &fallbackComma) {
    if (!var.isValid()) {
        return fallbackComma.split(',', Qt::SkipEmptyParts);
    }
    QStringList fromList = var.toStringList();
    if (!fromList.isEmpty()) {
        QStringList clean;
        for (const QString &item : fromList) {
            QString trimmed = item.trimmed();
            if (!trimmed.isEmpty()) clean << trimmed;
        }
        if (!clean.isEmpty()) return clean;
    }
    QString fromStr = var.toString().trimmed();
    if (!fromStr.isEmpty()) {
        return fromStr.split(',', Qt::SkipEmptyParts);
    }
    return fallbackComma.split(',', Qt::SkipEmptyParts);
}

void DrxProtocolConfig::load() {
    // Priority 1: applicationDirPath()/config/drx_protocols.ini
    // Priority 2: current working dir config/drx_protocols.ini
    QString appDir = QCoreApplication::applicationDirPath();
    QString candidate1 = appDir + "/config/drx_protocols.ini";
    QString candidate2 = QDir::currentPath() + "/config/drx_protocols.ini";

    if (QFile::exists(candidate1)) {
        m_filePath = candidate1;
    } else if (QFile::exists(candidate2)) {
        m_filePath = candidate2;
    } else {
        // Fallback target: candidate2 (in source/workspace root) or candidate1
        m_filePath = QFile::exists(QDir::currentPath() + "/NETA_Antenna_Test.pro") ? candidate2 : candidate1;
        ensureDefaultsFile(m_filePath);
    }

    QSettings settings(m_filePath, QSettings::IniFormat);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    settings.setIniCodec("UTF-8");
#endif

    // -------------------------------------------------------------------------
    // 1. REST / HTTP CGI Templates
    // -------------------------------------------------------------------------
    settings.beginGroup("REST_CGI");
    m_cgiEndpointPath = settings.value("EndpointPath", "/cgi-bin/drx.cgi").toString();
    m_cgiLoginFormat = settings.value("LoginPayloadFormat",
        "ResponseTo=login\nsessionKey=\nlg_username={username}\nlg_password={password}\n").toString();
    m_cgiStatusFormat = settings.value("StatusPayloadFormat",
        "query=alarmStatus\nsessionKey={sessionKey}\n").toString();
    m_cgiDeviceInfoFormat = settings.value("DeviceInfoPayloadFormat",
        "query=deviceInfo\nsessionKey={sessionKey}\n").toString();
    m_cgiTuneFormat = settings.value("TunePayloadFormat",
        "ResponseTo=freq\nsessionKey={sessionKey}\nfr_frequency={freqKhz}\nfr_srate={srate}\nfr_pol={pol}\n").toString();
    m_cgiRawQueryFormat = settings.value("RawQueryFormat",
        "query={query}\nsessionKey={sessionKey}\n").toString();

    m_cgiSessionKeyFields = parseIniStringList(settings.value("SessionKeyFields"), "sessionkey,sessionKey,key");
    m_cgiPowerFields = parseIniStringList(settings.value("PowerFields"), "power,pow,signal_power,rf_power");
    m_cgiCnFields = parseIniStringList(settings.value("CnFields"), "cn,snr,cn_db,c_n");
    m_cgiBerFields = parseIniStringList(settings.value("BerFields"), "ber,bit_error_rate");
    m_cgiEbN0Fields = parseIniStringList(settings.value("EbN0Fields"), "ebn0,eb_n0,ebno");
    m_cgiCarrierLockFields = parseIniStringList(settings.value("CarrierLockFields"), "carrier_lock,carrier,lock,signal_locked");
    m_cgiVideoLockFields = parseIniStringList(settings.value("VideoLockFields"), "video_lock,video,video_locked");
    m_cgiAudioLockFields = parseIniStringList(settings.value("AudioLockFields"), "audio_lock,audio,audio_locked");
    m_cgiChannelNameFields = parseIniStringList(settings.value("ChannelNameFields"), "channel_name,service_name,ch_name,channel");
    settings.endGroup();

    // -------------------------------------------------------------------------
    // 2. RS232 / Serial Protocol Templates
    // -------------------------------------------------------------------------
    settings.beginGroup("RS232_SERIAL");
    m_serialPromptTokens = parseIniStringList(settings.value("PromptDetectionTokens"), "-->,Select Option:,:");
    m_serialStatusPrefixes = parseIniStringList(settings.value("StatusResponsePrefixes"), "getstatus:,status:");
    m_serialStatusCmd = settings.value("StatusRequestFormat", "getstatus\n").toString();
    m_serialDeviceInfoCmd = settings.value("DeviceInfoRequestFormat", "getstatus\n").toString();
    m_serialTuneFormat = settings.value("TuneRequestFormat", "tune {freqMhz} {srate} {pol}\n").toString();
    m_serialTunerLogOnCmd = settings.value("TunerLogOnFormat", "tunerlog on\n").toString();
    m_serialTunerLogOffCmd = settings.value("TunerLogOffFormat", "tunerlog off\n").toString();

    m_serialCarrierLockToken = settings.value("CarrierLockToken", "S:+").toString();
    m_serialVideoLockToken = settings.value("VideoLockToken", "V:+").toString();
    m_serialAudioLockToken = settings.value("AudioLockToken", "A:+").toString();

    QString pPow = settings.value("PowerRegex", "POW:\\s*([+-]?[0-9.]+)\\s*(?:dBm)?").toString();
    QString pCn = settings.value("CnRegex", "C/N:\\s*([+-]?[0-9.]+)\\s*(?:dB)?").toString();
    QString pBer = settings.value("BerRegex", "BER:\\s*([0-9.eE+-]+)").toString();

    m_serialPowerRegex = QRegularExpression(pPow, QRegularExpression::CaseInsensitiveOption);
    m_serialCnRegex = QRegularExpression(pCn, QRegularExpression::CaseInsensitiveOption);
    m_serialBerRegex = QRegularExpression(pBer, QRegularExpression::CaseInsensitiveOption);
    settings.endGroup();

    // -------------------------------------------------------------------------
    // 3. RS485 / Industrial Bus Templates
    // -------------------------------------------------------------------------
    settings.beginGroup("RS485_BUS");
    m_rs485NodeAddress = settings.value("NodeAddress", 1).toInt();
    m_rs485FramingMode = settings.value("FramingMode", "ASCII").toString();
    m_rs485StatusFormat = settings.value("StatusRequestFormat", ":{addr:02d}0300000002\r\n").toString();
    m_rs485TuneFormat = settings.value("TuneRequestFormat", ":{addr:02d}06{freqMhz:04d}{srate:05d}{pol}\r\n").toString();
    settings.endGroup();

    qInfo() << "[DrxProtocolConfig] Loaded format templates from:" << m_filePath;
    emit protocolConfigChanged();
}

void DrxProtocolConfig::ensureDefaultsFile(const QString &path) {
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QFile file(path);
    if (file.exists()) return;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[DrxProtocolConfig] Failed to create default config file at:" << path;
        return;
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << "# ==============================================================================\n";
    out << "# NETA DRX9000 Protocol Formatting & Parsing Templates Configuration\n";
    out << "# Bu dosya, DRX alıcıları ile yapılan HTTP REST (CGI), RS232 ve RS485\n";
    out << "# komut şablonlarını ve gelen yanıtların ayrıştırma kurallarını içerir.\n";
    out << "# İstediğiniz zaman bu dosyayı düzenleyip formatları değiştirebilirsiniz.\n";
    out << "# ==============================================================================\n\n";

    out << "[REST_CGI]\n";
    out << "# İstek Gönderme Formatları (Placeholders: {sessionKey}, {username}, {password}, {freqKhz}, {srate}, {pol}, {query})\n";
    out << "EndpointPath=/cgi-bin/drx.cgi\n";
    out << "LoginPayloadFormat=ResponseTo=login\\nsessionKey=\\nlg_username={username}\\nlg_password={password}\\n\n";
    out << "StatusPayloadFormat=query=alarmStatus\\nsessionKey={sessionKey}\\n\n";
    out << "DeviceInfoPayloadFormat=query=deviceInfo\\nsessionKey={sessionKey}\\n\n";
    out << "TunePayloadFormat=ResponseTo=freq\\nsessionKey={sessionKey}\\nfr_frequency={freqKhz}\\nfr_srate={srate}\\nfr_pol={pol}\\n\n";
    out << "RawQueryFormat=query={query}\\nsessionKey={sessionKey}\\n\n";
    out << "# Gelen JSON Anahtar İsimleri (virgülle ayrılmış alternatif alan adları)\n";
    out << "SessionKeyFields=sessionkey,sessionKey,key\n";
    out << "PowerFields=power,pow,signal_power,rf_power\n";
    out << "CnFields=cn,snr,cn_db,c_n\n";
    out << "BerFields=ber,bit_error_rate\n";
    out << "EbN0Fields=ebn0,eb_n0,ebno\n";
    out << "CarrierLockFields=carrier_lock,carrier,lock,signal_locked\n";
    out << "VideoLockFields=video_lock,video,video_locked\n";
    out << "AudioLockFields=audio_lock,audio,audio_locked\n";
    out << "ChannelNameFields=channel_name,service_name,ch_name,channel\n\n";

    out << "[RS232_SERIAL]\n";
    out << "# Seri Port / USB-UART Komut Formatları (Placeholders: {freqMhz}, {srate}, {pol})\n";
    out << "PromptDetectionTokens=-->,Select Option:,:\n";
    out << "StatusResponsePrefixes=getstatus:,status:\n";
    out << "StatusRequestFormat=getstatus\\n\n";
    out << "DeviceInfoRequestFormat=getstatus\\n\n";
    out << "TuneRequestFormat=tune {freqMhz} {srate} {pol}\\n\n";
    out << "TunerLogOnFormat=tunerlog on\\n\n";
    out << "TunerLogOffFormat=tunerlog off\\n\n";
    out << "# Gelen ASCII Telemetri ve Kilit Bayrakları\n";
    out << "CarrierLockToken=S:+\n";
    out << "VideoLockToken=V:+\n";
    out << "AudioLockToken=A:+\n";
    out << "PowerRegex=POW:\\\\s*([+-]?[0-9.]+)\\\\s*(?:dBm)?\n";
    out << "CnRegex=C/N:\\\\s*([+-]?[0-9.]+)\\\\s*(?:dB)?\n";
    out << "BerRegex=BER:\\\\s*([0-9.eE+-]+)\n\n";

    out << "[RS485_BUS]\n";
    out << "# Endüstriyel Çok Noktalı (Multi-drop) RS485 Şablonu\n";
    out << "NodeAddress=1\n";
    out << "FramingMode=ASCII\n";
    out << "StatusRequestFormat=:{addr:02d}0300000002\\r\\n\n";
    out << "TuneRequestFormat=:{addr:02d}06{freqMhz:04d}{srate:05d}{pol}\\r\\n\n";

    file.close();
    qInfo() << "[DrxProtocolConfig] Created default protocol templates file at:" << path;
}

QString DrxProtocolConfig::replaceTokens(QString tmpl, const QMap<QString, QString> &tokens) {
    // Convert escaped newlines
    tmpl.replace("\\n", "\n");
    tmpl.replace("\\r", "\r");
    tmpl.replace("\\t", "\t");

    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        tmpl.replace("{" + it.key() + "}", it.value());
    }
    return tmpl;
}

QByteArray DrxProtocolConfig::formatCgiLogin(const QString &user, const QString &pass) const {
    QMap<QString, QString> t;
    t["username"] = user;
    t["password"] = pass;
    return replaceTokens(m_cgiLoginFormat, t).toUtf8();
}

QByteArray DrxProtocolConfig::formatCgiStatus(int sessionKey) const {
    QMap<QString, QString> t;
    t["sessionKey"] = QString::number(sessionKey);
    return replaceTokens(m_cgiStatusFormat, t).toUtf8();
}

QByteArray DrxProtocolConfig::formatCgiDeviceInfo(int sessionKey) const {
    QMap<QString, QString> t;
    t["sessionKey"] = QString::number(sessionKey);
    return replaceTokens(m_cgiDeviceInfoFormat, t).toUtf8();
}

QByteArray DrxProtocolConfig::formatCgiTune(int sessionKey, int freqKhz, int symbolRate, int polEnum) const {
    QMap<QString, QString> t;
    t["sessionKey"] = (sessionKey > 0) ? QString::number(sessionKey) : "";
    t["freqKhz"] = QString::number(freqKhz);
    t["freqMhz"] = QString::number(freqKhz / 1000);
    t["srate"] = QString::number(symbolRate);
    t["pol"] = QString::number(polEnum);
    return replaceTokens(m_cgiTuneFormat, t).toUtf8();
}

QByteArray DrxProtocolConfig::formatCgiRawQuery(const QString &query, int sessionKey) const {
    QString q = query.trimmed();

    // If query is already a multi-line or formatted key-value payload (contains '=' or newline)
    if (q.contains('=') || q.contains('\n') || q.contains('\r') || q.startsWith("ResponseTo", Qt::CaseInsensitive)) {
        QMap<QString, QString> t;
        t["sessionKey"] = (sessionKey > 0) ? QString::number(sessionKey) : "";
        QString result = replaceTokens(q, t);

        if (sessionKey > 0) {
            if (result.contains("sessionKey=\n")) {
                result.replace("sessionKey=\n", QString("sessionKey=%1\n").arg(sessionKey));
            } else if (result.contains("sessionKey=\r\n")) {
                result.replace("sessionKey=\r\n", QString("sessionKey=%1\r\n").arg(sessionKey));
            } else if (!result.contains("sessionKey=")) {
                if (!result.endsWith('\n')) result.append('\n');
                result.append(QString("sessionKey=%1\n").arg(sessionKey));
            }
        }
        return result.toUtf8();
    }

    // Single query token like "alarmStatus" or "deviceInfo"
    QMap<QString, QString> t;
    t["query"] = q;
    t["sessionKey"] = (sessionKey > 0) ? QString::number(sessionKey) : "";
    return replaceTokens(m_cgiRawQueryFormat, t).toUtf8();
}

double DrxProtocolConfig::extractFirstDouble(const QJsonObject &root, const QStringList &keys, double fallback) const {
    for (const QString &k : keys) {
        QString cleanKey = k.trimmed();
        if (root.contains(cleanKey)) {
            QJsonValue val = root.value(cleanKey);
            if (val.isDouble()) return val.toDouble();
            bool ok = false;
            double d = val.toString().toDouble(&ok);
            if (ok) return d;
        }
    }
    // Case-insensitive fallback
    for (const QString &k : keys) {
        QString cleanKey = k.trimmed().toLower();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key().trimmed().toLower() == cleanKey) {
                if (it.value().isDouble()) return it.value().toDouble();
                bool ok = false;
                double d = it.value().toString().toDouble(&ok);
                if (ok) return d;
            }
        }
    }
    return fallback;
}

bool DrxProtocolConfig::extractFirstBool(const QJsonObject &root, const QStringList &keys) const {
    auto evalVal = [](const QJsonValue &val) -> bool {
        if (val.isBool()) return val.toBool();
        if (val.isDouble()) return val.toInt() != 0;
        QString s = val.toString().trimmed().toUpper();
        return (s == "1" || s == "TRUE" || s == "YES" || s == "LOCKED" || s == "+" || s == "TICK" || s == "OK");
    };

    for (const QString &k : keys) {
        QString cleanKey = k.trimmed();
        if (root.contains(cleanKey)) {
            return evalVal(root.value(cleanKey));
        }
    }
    // Case-insensitive fallback
    for (const QString &k : keys) {
        QString cleanKey = k.trimmed().toLower();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key().trimmed().toLower() == cleanKey) {
                return evalVal(it.value());
            }
        }
    }
    return false;
}

QString DrxProtocolConfig::extractFirstString(const QJsonObject &root, const QStringList &keys) const {
    for (const QString &k : keys) {
        QString cleanKey = k.trimmed();
        if (root.contains(cleanKey)) {
            return root.value(cleanKey).toString();
        }
    }
    // Case-insensitive fallback
    for (const QString &k : keys) {
        QString cleanKey = k.trimmed().toLower();
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            if (it.key().trimmed().toLower() == cleanKey) {
                return it.value().toString();
            }
        }
    }
    return QString();
}

int DrxProtocolConfig::extractSessionKey(const QJsonObject &root) const {
    // 1. Check configured fields
    for (const QString &k : m_cgiSessionKeyFields) {
        QString cleanKey = k.trimmed();
        if (root.contains(cleanKey)) {
            QJsonValue val = root.value(cleanKey);
            if (val.isDouble()) return val.toInt();
            bool ok = false;
            int key = val.toString().toInt(&ok);
            if (ok && key > 0) return key;
        }
    }

    // 2. Direct fail-safe check for common DRX key variants
    const QStringList fallbacks = {"sessionkey", "sessionKey", "key", "SessionKey", "Sessionkey", "sessKey", "Session_Key"};
    for (const QString &fk : fallbacks) {
        if (root.contains(fk)) {
            QJsonValue val = root.value(fk);
            if (val.isDouble()) return val.toInt();
            bool ok = false;
            int key = val.toString().toInt(&ok);
            if (ok && key > 0) return key;
        }
    }

    // 3. Deep case-insensitive scan across root keys
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key().compare("sessionkey", Qt::CaseInsensitive) == 0 ||
            it.key().compare("session_key", Qt::CaseInsensitive) == 0) {
            if (it.value().isDouble()) return it.value().toInt();
            bool ok = false;
            int key = it.value().toString().toInt(&ok);
            if (ok && key > 0) return key;
        }
    }

    return -1;
}

double DrxProtocolConfig::extractPower(const QJsonObject &root, double fallback) const {
    return extractFirstDouble(root, m_cgiPowerFields, fallback);
}

double DrxProtocolConfig::extractCn(const QJsonObject &root, double fallback) const {
    return extractFirstDouble(root, m_cgiCnFields, fallback);
}

double DrxProtocolConfig::extractBer(const QJsonObject &root, double fallback) const {
    return extractFirstDouble(root, m_cgiBerFields, fallback);
}

double DrxProtocolConfig::extractEbN0(const QJsonObject &root, double fallback) const {
    return extractFirstDouble(root, m_cgiEbN0Fields, fallback);
}

bool DrxProtocolConfig::extractCarrierLock(const QJsonObject &root) const {
    return extractFirstBool(root, m_cgiCarrierLockFields);
}

bool DrxProtocolConfig::extractVideoLock(const QJsonObject &root) const {
    return extractFirstBool(root, m_cgiVideoLockFields);
}

bool DrxProtocolConfig::extractAudioLock(const QJsonObject &root) const {
    return extractFirstBool(root, m_cgiAudioLockFields);
}

QString DrxProtocolConfig::extractChannelName(const QJsonObject &root) const {
    return extractFirstString(root, m_cgiChannelNameFields);
}

QString DrxProtocolConfig::formatSerialTune(int freqMhz, int symbolRate, const QString &polarity) const {
    QMap<QString, QString> t;
    t["freqMhz"] = QString::number(freqMhz);
    t["srate"] = QString::number(symbolRate);
    t["pol"] = polarity.trimmed().toUpper();
    return replaceTokens(m_serialTuneFormat, t);
}

bool DrxProtocolConfig::isPromptLine(const QString &line) const {
    for (const QString &token : m_serialPromptTokens) {
        QString cleanToken = token.trimmed();
        if (!cleanToken.isEmpty() && line.contains(cleanToken)) {
            return true;
        }
    }
    return false;
}

bool DrxProtocolConfig::isStatusResponseLine(const QString &line) const {
    for (const QString &pfx : m_serialStatusPrefixes) {
        QString cleanPfx = pfx.trimmed();
        if (!cleanPfx.isEmpty() && line.contains(cleanPfx, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void DrxProtocolConfig::parseSerialLocks(const QString &line, bool &outCarrier, bool &outVideo, bool &outAudio) const {
    outCarrier = (!m_serialCarrierLockToken.isEmpty() && line.contains(m_serialCarrierLockToken));
    outVideo = (!m_serialVideoLockToken.isEmpty() && line.contains(m_serialVideoLockToken));
    outAudio = (!m_serialAudioLockToken.isEmpty() && line.contains(m_serialAudioLockToken));
}

bool DrxProtocolConfig::parseSerialTelemetry(const QString &line, double &outPower, double &outCn, double &outBer) const {
    bool matched = false;

    QRegularExpressionMatch mPow = m_serialPowerRegex.match(line);
    if (mPow.hasMatch()) {
        outPower = mPow.captured(1).toDouble();
        matched = true;
    }

    QRegularExpressionMatch mCn = m_serialCnRegex.match(line);
    if (mCn.hasMatch()) {
        outCn = mCn.captured(1).toDouble();
        matched = true;
    }

    QRegularExpressionMatch mBer = m_serialBerRegex.match(line);
    if (mBer.hasMatch()) {
        outBer = mBer.captured(1).toDouble();
        matched = true;
    } else {
        outBer = 1.0e-7;
    }

    return matched && (outCn != 0 || outPower != 0);
}

QString DrxProtocolConfig::formatRs485StatusCmd() const {
    QMap<QString, QString> t;
    t["addr"] = QString("%1").arg(m_rs485NodeAddress, 2, 10, QChar('0'));
    return replaceTokens(m_rs485StatusFormat, t);
}

QString DrxProtocolConfig::formatRs485TuneCmd(int freqMhz, int symbolRate, const QString &polarity) const {
    QMap<QString, QString> t;
    t["addr"] = QString("%1").arg(m_rs485NodeAddress, 2, 10, QChar('0'));
    t["freqMhz"] = QString::number(freqMhz);
    t["srate"] = QString::number(symbolRate);
    t["pol"] = polarity.trimmed().toUpper();
    return replaceTokens(m_rs485TuneFormat, t);
}
