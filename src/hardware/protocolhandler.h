/**
 * @file protocolhandler.h
 * @brief Stream processor and frame parser for DRX9000 ASCII/binary console protocol.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef PROTOCOLHANDLER_H
#define PROTOCOLHANDLER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include "protocol_commands.h"

/**
 * @class ProtocolHandler
 * @brief Strips ANSI escape sequences, frames serial incoming byte streams,
 *        and parses status, telemetry, and device metadata.
 */
class ProtocolHandler : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolHandler(QObject *parent = nullptr);

    // Command packet construction
    QByteArray buildPacket(const QString &commandMacro);
    QByteArray buildPacketWithArgs(const QString &commandMacro, const QStringList &args);

    // Stream buffering and line framing
    void processBuffer(QByteArray &rxBuffer);

    // Packet parsing utilities
    bool parseIncomingData(const QByteArray &rawData, QString &outParsedMessage, bool &outIsSuccess);
    bool parseStatusLine(const QString &line, bool &signalLocked, bool &videoProducing, bool &audioProducing);
    bool parseRfTelemetryLine(const QString &line, QString &outChannel, double &outFreq, QString &outPol, double &outPow, double &outCn, double &outBer, double &outEbN0);

    // Checksum utility
    quint8 calculateChecksum8(const QByteArray &data);

signals:
    void packetPrepared(const QByteArray &packet);
    void logEmitted(const QString &logText, bool isError);

    // Parsed Receiver Event Signals
    void statusParsed(bool signalLocked, bool videoProducing, bool audioProducing);
    void channelHeaderParsed(int totalChannels);
    void channelItemParsed(int index, const QString &name, int serviceId);
    void tunerLogSampleParsed(double value);
    void rfTelemetryParsed(const QString &channelName, double freqMHz, const QString &polarity, double powerDbm, double cnDb, double ber, double ebN0Db);
    void commandResponseParsed(const QString &cmdName, bool isOk, const QString &details);
    void interactivePromptDetected(const QString &promptLine);
    void deviceInfoParsed(const QString &key, const QString &value);

private:
    QString extractValueAfterKey(const QString &line, const QString &key);
    QString m_pendingInfoKey;
};

#endif // PROTOCOLHANDLER_H
