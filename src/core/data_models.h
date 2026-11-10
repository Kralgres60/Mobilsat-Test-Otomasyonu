/**
 * @file data_models.h
 * @brief Common data structures, configuration models, and serialization helpers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DATA_MODELS_H
#define DATA_MODELS_H

#include <QString>
#include <QVector>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @enum UserRole
 * @brief User privilege levels: Technician (simplified operator) vs Engineer (admin/supervisor).
 */
enum class UserRole {
    Technician, ///< Operator / Technician: Simple test execution, barcode scan, pass/fail status
    Engineer    ///< Engineer / Admin: Full access, tolerances, frequency lists, calibration, diagnostics
};

/**
 * @brief Satellite transponder channel configuration model.
 */
struct ChannelConfig {
    int channelNo = 1;              ///< Channel sequence number (1..10)
    QString channelName;            ///< User-friendly channel/station label (e.g., "TRT 1 HD", "NTV")
    int frequencyMhz = 11054;       ///< Transponder downlink frequency in MHz
    int symbolRate = 30000;         ///< Symbol rate in kSym/s
    QString polarization = "V";     ///< Polarization: "V" (Vertical) or "H" (Horizontal)
    double toleranceDb = 1.50;      ///< Maximum allowed RF attenuation delta compared to Golden Sample (dB)

    /** @brief Serializes channel configuration to JSON object. */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["channel_no"] = channelNo;
        obj["channel_name"] = channelName;
        obj["freq_mhz"] = frequencyMhz;
        obj["sr"] = symbolRate;
        obj["pol"] = polarization;
        obj["tolerance_db"] = toleranceDb;
        return obj;
    }

    /** @brief Deserializes channel configuration from JSON object. */
    static ChannelConfig fromJson(const QJsonObject &obj) {
        ChannelConfig c;
        c.channelNo = obj["channel_no"].toInt();
        c.channelName = obj["channel_name"].toString();
        c.frequencyMhz = obj["freq_mhz"].toInt();
        c.symbolRate = obj["sr"].toInt();
        c.polarization = obj["pol"].toString();
        c.toleranceDb = obj["tolerance_db"].toDouble(1.50);
        return c;
    }
};

/**
 * @brief Telemetry sample result for a single transponder channel.
 */
struct ChannelSampleResult {
    int channelNo = 1;              ///< Channel index (1..10)
    QString channelName;            ///< Transponder channel name
    int frequencyMhz = 0;           ///< Transponder frequency in MHz
    int symbolRate = 0;             ///< Symbol rate in kSym/s
    QString polarization;           ///< Polarization ("V" or "H")
    
    double dutCn = 0.0;             ///< DUT antenna averaged C/N (dB)
    double dutPow = 0.0;            ///< DUT antenna averaged RF power (dBm)
    double refCn = 0.0;             ///< Reference Golden Sample averaged C/N (dB)
    double refPow = 0.0;            ///< Reference Golden Sample averaged RF power (dBm)
    double deltaDb = 0.0;           ///< Delta attenuation: dutCn - refCn (dB)
    
    bool carrierLocked = false;     ///< Carrier / RF signal locked flag (S:+)
    bool videoLocked = false;       ///< Video stream locked flag (V:+)
    bool audioLocked = false;       ///< Audio stream locked flag (A:+)
    double toleranceDb = 1.50;      ///< Allowed tolerance limit (dB)
    QString status = "PENDING";     ///< Channel status: PENDING, PASS, FAIL, WARNING

    /** @brief Serializes sample result to JSON object. */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["channel_no"] = channelNo;
        obj["channel_name"] = channelName;
        obj["freq_mhz"] = frequencyMhz;
        obj["sr"] = symbolRate;
        obj["pol"] = polarization;
        obj["dut_cn"] = dutCn;
        obj["dut_pow"] = dutPow;
        obj["ref_cn"] = refCn;
        obj["ref_pow"] = refPow;
        obj["delta_db"] = deltaDb;
        obj["carrier_locked"] = carrierLocked;
        obj["video_locked"] = videoLocked;
        obj["audio_locked"] = audioLocked;
        obj["tolerance_db"] = toleranceDb;
        obj["status"] = status;
        return obj;
    }

    /** @brief Deserializes sample result from JSON object. */
    static ChannelSampleResult fromJson(const QJsonObject &obj) {
        ChannelSampleResult r;
        r.channelNo = obj["channel_no"].toInt();
        r.channelName = obj["channel_name"].toString();
        r.frequencyMhz = obj["freq_mhz"].toInt();
        r.symbolRate = obj["sr"].toInt();
        r.polarization = obj["pol"].toString();
        r.dutCn = obj["dut_cn"].toDouble();
        r.dutPow = obj["dut_pow"].toDouble();
        r.refCn = obj["ref_cn"].toDouble();
        r.refPow = obj["ref_pow"].toDouble();
        r.deltaDb = obj["delta_db"].toDouble();
        r.carrierLocked = obj["carrier_locked"].toBool();
        r.videoLocked = obj["video_locked"].toBool();
        r.audioLocked = obj["audio_locked"].toBool();
        r.toleranceDb = obj["tolerance_db"].toDouble(1.50);
        r.status = obj["status"].toString();
        return r;
    }
};

/**
 * @brief Comprehensive test run summary record for an individual antenna.
 */
struct TestRunData {
    qint64 id = 0;                  ///< Primary database run ID
    int workOrderId = 0;            ///< Associated factory work order ID
    int stationId = 1;              ///< Workstation ID (1, 2, or 3)
    int operatorId = 1;             ///< Quality assurance operator ID
    QString antennaSn;              ///< Unique serial number scanned from barcode
    int satelliteProfileId = 1;     ///< Target satellite test profile ID
    QString profileName;            ///< Satellite profile name (e.g., "Turksat 42.0E Standard")
    QDateTime timestamp;            ///< Test execution start timestamp
    int durationSec = 0;            ///< Total test execution elapsed time in seconds
    QString overallResult = "PENDING"; ///< Overall verdict: PASS, FAIL, ABORTED
    QString failReason;             ///< Detailed root-cause reason if failed
    double avgDeltaDb = 0.0;        ///< Arithmetic mean delta across all transponders (dB)
    double maxDeltaDb = 0.0;        ///< Worst-case maximum delta across all transponders (dB)
    QString notes;                  ///< Operator or system observations
    QVector<ChannelSampleResult> channels; ///< Detailed results for all transponder channels

    /** @brief Serializes complete test run record to JSON object. */
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["work_order_id"] = workOrderId;
        obj["station_id"] = stationId;
        obj["operator_id"] = operatorId;
        obj["antenna_sn"] = antennaSn;
        obj["satellite_profile_id"] = satelliteProfileId;
        obj["profile_name"] = profileName;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        obj["duration_sec"] = durationSec;
        obj["overall_result"] = overallResult;
        obj["fail_reason"] = failReason;
        obj["avg_delta_db"] = avgDeltaDb;
        obj["max_delta_db"] = maxDeltaDb;
        obj["notes"] = notes;

        QJsonArray chArray;
        for (const auto &ch : channels) {
            chArray.append(ch.toJson());
        }
        obj["channels"] = chArray;
        return obj;
    }
};

/**
 * @brief Factory work order information model.
 */
struct WorkOrderInfo {
    int id = 0;                     ///< Work order primary key
    QString orderNo;                ///< Factory order number (e.g., "WO-2026-0042")
    QString customerName;           ///< Client or project name
    QString productCode;            ///< Antenna product model code (e.g., "DRX9000-M")
    int satelliteProfileId = 1;     ///< Assigned satellite profile ID
    int targetQuantity = 0;         ///< Production target lot size
    int completedQuantity = 0;      ///< Total processed antennas
    int passedQuantity = 0;         ///< Approved antennas
    int failedQuantity = 0;         ///< Rejected antennas
    QString status = "PENDING";     ///< Status: PENDING, IN_PROGRESS, COMPLETED
    QString notes;                  ///< Order notes
};

/**
 * @brief Satellite orbital position and transponder profile configuration.
 */
struct SatelliteProfileInfo {
    int id = 0;                     ///< Profile primary key
    QString profileName;            ///< Profile identifier (e.g., "Turksat 42E Standard")
    QString satelliteName;          ///< Satellite constellation name
    QString orbitalPosition = "42.0E"; ///< Orbital slot (e.g., "42.0E")
    double defaultToleranceDb = 1.50;  ///< Default delta tolerance (dB)
    bool isActive = true;           ///< Active status flag
    QVector<ChannelConfig> channels;///< Transponder channels in this profile
};

/**
 * @brief Live station state model for central monitoring dashboards.
 */
struct StationLiveStatus {
    int stationId = 1;              ///< Station numeric ID (1..3)
    QString stationName;            ///< Display name (e.g., "Station #1")
    QString ipAddress;              ///< Workstation IPv4 address
    bool isConnected = false;       ///< Network connectivity flag
    QString currentState = "DISCONNECTED"; ///< FSM state: DISCONNECTED, IDLE, WAITING_LOCK, TESTING, COMPLETED
    int currentChannel = 0;         ///< Currently active channel index
    QString currentAntennaSn;       ///< Currently tested antenna serial number
    double lastDeltaDb = 0.0;       ///< Last observed delta attenuation (dB)
    QDateTime lastHeartbeat;        ///< Last received keep-alive timestamp
};

#endif // DATA_MODELS_H
