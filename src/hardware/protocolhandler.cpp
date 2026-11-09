#include "protocolhandler.h"
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <cstdio>

ProtocolHandler::ProtocolHandler(QObject *parent)
    : QObject(parent)
{
}

QByteArray ProtocolHandler::buildPacket(const QString &commandString)
{
    QByteArray packet;
    
    // Header ekle (Eğer tanımlıysa)
    if (!NetaProtocol::HEADER.isEmpty()) {
        packet.append(NetaProtocol::HEADER.toUtf8());
    }

    // Komut gövdesi
    packet.append(commandString.toUtf8());

    // Footer / Paket sonlandırıcı (Eğer komutun sonunda yoksa ekler)
    if (!commandString.endsWith(NetaProtocol::FOOTER) && !NetaProtocol::FOOTER.isEmpty()) {
        packet.append(NetaProtocol::FOOTER.toUtf8());
    }

    emit packetPrepared(packet);
    return packet;
}

QByteArray ProtocolHandler::buildPacketWithArgs(const QString &commandString, const QStringList &args)
{
    QString formattedCmd = commandString.trimmed();
    for (int i = 0; i < args.size(); ++i) {
        formattedCmd = formattedCmd.arg(args.at(i));
    }
    return buildPacket(formattedCmd);
}

void ProtocolHandler::processBuffer(QByteArray &rxBuffer)
{
    // ANSI renk kodlarını (\x1b[32m vb.) temizle
    static const QRegularExpression ansiRegex("\x1b\\[[0-9;]*[a-zA-Z]");
    
    // Cihazdan gelen NULL (\0) karakterlerini boşluğa çevir
    rxBuffer.replace('\0', ' ');

    while (rxBuffer.contains('\n') || rxBuffer.contains('\r')) {
        int indexLf = rxBuffer.indexOf('\n');
        int indexCr = rxBuffer.indexOf('\r');

        int index = -1;
        if (indexLf != -1 && indexCr != -1) {
            index = qMin(indexLf, indexCr);
        } else if (indexLf != -1) {
            index = indexLf;
        } else {
            index = indexCr;
        }

        // Satırı kes ve QString'e dönüştür (C-String truncation önlemek için explicit size ile)
        QByteArray lineBytes = rxBuffer.left(index);
        rxBuffer.remove(0, index + 1);

        QString line = QString::fromUtf8(lineBytes.constData(), lineBytes.size()).remove(ansiRegex).trimmed();
        if (line.isEmpty()) {
            continue;
        }

        emit logEmitted(line, false);

        // 1. İnteraktif Menü / Prompt Algılama
        if (line.contains("-->") || line.contains("Select Option:") || line.endsWith(":")) {
            emit interactivePromptDetected(line);
        }

        // 2. Durum Paketleri (Örn: getstatus:S:+ V:+ A:+)
        if (line.contains("getstatus:")) {
            bool signalLocked = line.contains("S:+");
            bool videoProducing = line.contains("V:+");
            bool audioProducing = line.contains("A:+");
            emit statusParsed(signalLocked, videoProducing, audioProducing);
            emit commandResponseParsed("getstatus", true, line);
        }
        // 3. Kanal Listesi Üst Bilgisi (Örn: listchannels:Num Channels:3 veya Channels (Total: 45):)
        else if (line.contains("Num Channels:", Qt::CaseInsensitive) || line.contains("Channels (Total:", Qt::CaseInsensitive) || line.contains("Channel Count:", Qt::CaseInsensitive)) {
            QRegularExpression rx("(?:Num Channels:|Total:)\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = rx.match(line);
            if (match.hasMatch()) {
                int count = match.captured(1).toInt();
                emit channelHeaderParsed(count);
            }
        }
        // 4. Kanal Öğesi Satırı (Örn: 0:KANAL 7 SID:11500, veya 0:01 ˹  SID:1010,)
        else if (line.contains("SID:", Qt::CaseInsensitive) || line.contains("SID=", Qt::CaseInsensitive)) {
            QRegularExpression rxA("(\\d+):\\s*(.+)\\s+SID:(\\d+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpression rxB("\\[?(\\d+)\\]?\\.?\\s*([^\\[]+)\\[SID:\\s*(\\d+)\\]", QRegularExpression::CaseInsensitiveOption);

            QRegularExpressionMatch matchA = rxA.match(line);
            QRegularExpressionMatch matchB = rxB.match(line);

            if (matchA.hasMatch()) {
                int idx = matchA.captured(1).toInt();
                QString rawName = matchA.captured(2).trimmed();
                // Baştaki rakam ve garip karakterleri temizle (Örn: "01 ˹ " -> "Kanal 1010")
                QString cleanName = rawName;
                cleanName.remove(QRegularExpression("[^a-zA-Z0-9çğıöşüÇĞİÖŞÜ\\- _\\+\\.\\/\\(\\)]"));
                cleanName = cleanName.trimmed();
                if (cleanName.length() < 2) {
                    cleanName = QString("Kanal %1").arg(matchA.captured(3));
                }
                int sid = matchA.captured(3).toInt();
                emit channelItemParsed(idx, cleanName, sid);
            } else if (matchB.hasMatch()) {
                int idx = matchB.captured(1).toInt();
                QString rawName = matchB.captured(2).trimmed();
                QString cleanName = rawName;
                cleanName.remove(QRegularExpression("[^a-zA-Z0-9çğıöşüÇĞİÖŞÜ\\- _\\+\\.\\/\\(\\)]"));
                cleanName = cleanName.trimmed();
                if (cleanName.length() < 2) {
                    cleanName = QString("Kanal %1").arg(matchB.captured(3));
                }
                int sid = matchB.captured(3).toInt();
                emit channelItemParsed(idx, cleanName, sid);
            }
        }
        // 5. Canlı Tuner Telemetri Verisi (Sadece sayısal veya tunerlog:)
        else if (line.startsWith("tunerlog:", Qt::CaseInsensitive) || line.startsWith("TL:", Qt::CaseInsensitive) || line.startsWith("tunerlog", Qt::CaseInsensitive)) {
            QString valStr = line.section(':', 1, 1).trimmed();
            bool ok = false;
            double val = valStr.toDouble(&ok);
            if (ok) {
                emit tunerLogSampleParsed(val);
            }
        }
        // 6. RS232 RF Telemetri Satırı Yakalama (POW, C/N veya BER geçen satırlar)
        else if (line.contains("POW:", Qt::CaseInsensitive) || line.contains("C/N:", Qt::CaseInsensitive) || line.contains("BER:", Qt::CaseInsensitive) || line.contains("EbN0:", Qt::CaseInsensitive)) {
            QString ch; double freq = 0, pow = 0, cn = 0, ber = 0, ebn0 = 0; QString pol;
            if (parseRfTelemetryLine(line, ch, freq, pol, pow, cn, ber, ebn0)) {
                emit rfTelemetryParsed(ch, freq, pol, pow, cn, ber, ebn0);
                emit tunerLogSampleParsed(cn != 0 ? cn : pow);
            }
        }

        // 7. Cihaz Açılış ve Donanım Telemetrisi Yakalama
        else if (line.contains("SW Ver", Qt::CaseInsensitive)) {
            QString val = extractValueAfterKey(line, "SW Ver");
            if (!val.isEmpty()) {
                emit deviceInfoParsed("swVer", val);
                m_pendingInfoKey.clear();
            } else {
                m_pendingInfoKey = "swVer";
            }
        }
        else if (line.contains("HW Ver", Qt::CaseInsensitive)) {
            QString val = extractValueAfterKey(line, "HW Ver");
            if (!val.isEmpty()) {
                emit deviceInfoParsed("hwVer", val);
                m_pendingInfoKey.clear();
            } else {
                m_pendingInfoKey = "hwVer";
            }
        }
        else if (line.contains("NETA DRX", Qt::CaseInsensitive) || line.contains("terminal", Qt::CaseInsensitive)) {
            emit deviceInfoParsed("model", line);
            m_pendingInfoKey.clear();
        }
        else if (line.contains("Board:", Qt::CaseInsensitive) || line.contains("STx7105", Qt::CaseInsensitive)) {
            QString board = extractValueAfterKey(line, "Board");
            if (board.isEmpty()) board = "STx7105-Mboard";
            emit deviceInfoParsed("chipset", board);
            m_pendingInfoKey.clear();
        }
        else if (line.contains("MAC", Qt::CaseInsensitive) || line.contains("eth", Qt::CaseInsensitive) || line.contains("PHY", Qt::CaseInsensitive)) {
            QRegularExpression rxMac("([0-9a-fA-F]{2}[:\\-][0-9a-fA-F]{2}[:\\-][0-9a-fA-F]{2}[:\\-][0-9a-fA-F]{2}[:\\-][0-9a-fA-F]{2}[:\\-][0-9a-fA-F]{2})");
            QRegularExpressionMatch match = rxMac.match(line);
            if (match.hasMatch()) {
                emit deviceInfoParsed("mac", match.captured(1));
            }
        }
        else if (!m_pendingInfoKey.isEmpty()) {
            emit deviceInfoParsed(m_pendingInfoKey, line.trimmed());
            m_pendingInfoKey.clear();
        }

        // 8. Komut Başarılı / Hata Yanıtları
        else if (line.endsWith(":ok") || line.contains("Command Executed Successfully", Qt::CaseInsensitive)) {
            QString cmdName = line.section(':', 0, 0).trimmed();
            emit commandResponseParsed(cmdName, true, line);
        }
        else if (line.contains(":error") || line.contains("Invalid Command", Qt::CaseInsensitive)) {
            QString cmdName = line.section(':', 0, 0).trimmed();
            emit commandResponseParsed(cmdName, false, line);
        }
    }
}

QString ProtocolHandler::extractValueAfterKey(const QString &line, const QString &key)
{
    int keyPos = line.indexOf(key, 0, Qt::CaseInsensitive);
    if (keyPos == -1) return QString();

    int startPos = keyPos + key.length();
    while (startPos < line.length() && (line[startPos] == ':' || line[startPos] == ' ' || line[startPos] == '\t')) {
        startPos++;
    }

    if (startPos >= line.length()) return QString();

    QString val = line.mid(startPos).trimmed();
    int endPos = val.indexOf(QRegularExpression("[\\s,\\;]"));
    if (endPos != -1) {
        val = val.left(endPos).trimmed();
    }
    return val;
}

bool ProtocolHandler::parseRfTelemetryLine(const QString &line, QString &outChannel, double &outFreq, QString &outPol, double &outPow, double &outCn, double &outBer, double &outEbN0)
{
    // Esnek regex parsing: N:KANAL 7  F:12015Mhz  P:H  POW:-39 dBm  C/N:17.0 dB  BER:0.0E-7  EbN0:15.58 dB
    static QRegularExpression rx("N:\\s*(.+?)\\s+F:\\s*(\\d+)Mhz\\s+P:\\s*(\\w+)\\s+POW:\\s*([\\d.-]+)\\s*dBm\\s+C/N:\\s*([\\d.-]+)\\s*dB\\s+BER:\\s*([\\S]+)\\s+EbN0:\\s*([\\d.-]+)", QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch match = rx.match(line);
    if (match.hasMatch()) {
        outChannel = match.captured(1).trimmed();
        outFreq = match.captured(2).toDouble();
        outPol = match.captured(3).trimmed();
        outPow = match.captured(4).toDouble();
        outCn = match.captured(5).toDouble();
        outBer = match.captured(6).toDouble();
        outEbN0 = match.captured(7).toDouble();
        return true;
    }

    // Yedek Parçalama (Eğer kanal adında garip karakterler varsa)
    if (line.contains("POW:", Qt::CaseInsensitive) && line.contains("C/N:", Qt::CaseInsensitive)) {
        outChannel = extractValueAfterKey(line, "N:").trimmed();
        outFreq = extractValueAfterKey(line, "F:").remove("Mhz", Qt::CaseInsensitive).toDouble();
        outPol = extractValueAfterKey(line, "P:");
        outPow = extractValueAfterKey(line, "POW:").remove("dBm", Qt::CaseInsensitive).toDouble();
        outCn = extractValueAfterKey(line, "C/N:").remove("dB", Qt::CaseInsensitive).toDouble();
        outBer = extractValueAfterKey(line, "BER:").toDouble();
        outEbN0 = extractValueAfterKey(line, "EbN0:").remove("dB", Qt::CaseInsensitive).toDouble();
        return (outCn != 0 || outPow != 0);
    }
    return false;
}

bool ProtocolHandler::parseIncomingData(const QByteArray &rawData, QString &outParsedMessage, bool &outIsSuccess)
{
    if (rawData.isEmpty()) {
        outIsSuccess = false;
        outParsedMessage = "Boş veri paketi";
        return false;
    }

    QString dataStr = QString::fromUtf8(rawData).trimmed();
    outParsedMessage = dataStr;

    if (dataStr.contains(NetaProtocol::RESP_OK) || dataStr.contains(NetaProtocol::RESP_ACK) || dataStr.contains("ok")) {
        outIsSuccess = true;
    } else if (dataStr.contains(NetaProtocol::RESP_ERROR)) {
        outIsSuccess = false;
    } else {
        outIsSuccess = true;
    }

    return outIsSuccess;
}

quint8 ProtocolHandler::calculateChecksum8(const QByteArray &data)
{
    quint8 checksum = 0;
    for (char c : data) {
        checksum ^= static_cast<quint8>(c);
    }
    return checksum;
}
