/**
 * @file network_messages.h
 * @brief TCP and UDP message definitions, frame types, and JSON serialization utilities.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef NETWORK_MESSAGES_H
#define NETWORK_MESSAGES_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>

namespace NetMsg {
    // Protocol Message Types
    inline const QString TYPE_BEACON        = "NETA_BEACON";      ///< UDP Auto-discovery broadcast beacon
    inline const QString TYPE_REGISTER      = "REGISTER";         ///< Client station handshake registration
    inline const QString TYPE_REGISTER_ACK  = "REGISTER_ACK";     ///< Server registration acknowledgement
    inline const QString TYPE_HEARTBEAT     = "HEARTBEAT";        ///< Client-to-server keep-alive ping
    inline const QString TYPE_LOCK_REQ      = "LOCK_REQ";         ///< Request exclusive lock on Reference DRX
    inline const QString TYPE_LOCK_GRANTED  = "LOCK_GRANTED";     ///< Reference hardware lock granted
    inline const QString TYPE_LOCK_WAITING  = "LOCK_WAITING";     ///< Enqueued in FIFO lock waiting queue
    inline const QString TYPE_LOCK_RELEASE  = "LOCK_RELEASE";     ///< Request lock release
    inline const QString TYPE_LOCK_RELEASED = "LOCK_RELEASED";    ///< Lock release confirmation
    inline const QString TYPE_LOCK_EXPIRED  = "LOCK_EXPIRED";     ///< Guard timeout expiration notice
    inline const QString TYPE_REF_TUNE        = "REF_TUNE";         ///< Request Reference DRX to tune transponder
    inline const QString TYPE_REF_TUNE_ACK    = "REF_TUNE_ACK";     ///< Reference DRX tune confirmation
    inline const QString TYPE_REF_DATA        = "REF_DATA";         ///< Streaming Golden Sample RF telemetry
    inline const QString TYPE_REF_MODE_UPDATE = "REF_MODE_UPDATE";  ///< Broadcast active reference mode (LIVE vs GOLDEN)

    /**
     * @brief Serializes a QJsonObject to a newline-terminated JSON stream.
     * @param obj Input JSON object
     * @return Compact UTF-8 QByteArray terminated with '\\n'
     */
    inline QByteArray serialize(const QJsonObject &obj) {
        return QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    }

    /**
     * @brief Deserializes a newline-delimited byte buffer into a QJsonObject.
     * @param data Raw JSON bytes
     * @return Extracted QJsonObject or empty object on error
     */
    inline QJsonObject deserialize(const QByteArray &data) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            return doc.object();
        }
        return QJsonObject();
    }
}

#endif // NETWORK_MESSAGES_H
