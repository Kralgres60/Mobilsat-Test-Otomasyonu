/**
 * @file sim_curve_model.h
 * @brief Editable RF telemetry curve data model with preset generators and JSON/CSV import-export.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SIM_CURVE_MODEL_H
#define SIM_CURVE_MODEL_H

#include <QString>
#include <QVector>

/**
 * @struct SimTelemetryPoint
 * @brief Represents a single time/step sample in the virtual DRX9000 RF telemetry curve.
 */
struct SimTelemetryPoint {
    double cnDb       = 14.50;   ///< Carrier-to-Noise ratio in dB (typical 0.0 .. 25.0)
    double powerDbm   = -42.00;  ///< RF input signal level in dBm (typical -90.0 .. -10.0)
    bool   signalLock = true;    ///< Tuner RF lock state
    bool   videoLock  = true;    ///< Video elementary stream decode state
    bool   audioLock  = true;    ///< Audio elementary stream decode state
    QString ber       = "1.0E-7";///< Bit Error Rate string
    QString note      = "Normal";///< User annotation or fault description

    /**
     * @brief Returns true if this step represents a degraded or fault condition.
     * @return True when lock is lost or C/N is below typical DVB-S2 threshold (9.0 dB).
     */
    bool isFaulty() const {
        return !signalLock || !videoLock || !audioLock || cnDb < 9.0;
    }
};

/**
 * @class SimCurveModel
 * @brief Utility container providing curve presets and JSON/CSV serialization
 *        for repeatable RF fault-injection testing.
 */
class SimCurveModel {
public:
    /**
     * @enum PresetType
     * @brief Built-in RF curve profiles for rapid fault testing.
     */
    enum class PresetType {
        NormalStable,       ///< Steady 14.5 dB C/N, -42 dBm, full locks (PASS profile)
        MiddleCnDropFault,  ///< Smooth C/N dip in middle steps to trigger FAIL detection
        BriefSignalLoss,    ///< Temporary lock drop (Signal/Video/Audio cross) mid-stream
        SteppedDecay        ///< Gradual signal degradation across steps
    };

    /**
     * @brief Generates a sample curve matching one of the built-in presets.
     * @param type Preset profile type.
     * @param pointCount Number of steps to generate (clamped to [5, 200]).
     * @return Vector of generated telemetry points.
     */
    static QVector<SimTelemetryPoint> createPreset(PresetType type, int pointCount = 30);

    /**
     * @brief Exports the telemetry curve and playback metadata to a JSON file.
     * @param filePath Destination file path (.json).
     * @param points Curve points to serialize.
     * @param curveName Friendly profile name stored in metadata.
     * @param sampleIntervalMs Playback step interval in milliseconds.
     * @param outError Optional error description on failure.
     * @return True if export succeeded.
     */
    static bool exportJson(const QString &filePath,
                           const QVector<SimTelemetryPoint> &points,
                           const QString &curveName,
                           int sampleIntervalMs,
                           QString *outError = nullptr);

    /**
     * @brief Imports a telemetry curve and playback metadata from a JSON file.
     * @param filePath Source file path (.json).
     * @param outPoints Populated curve points on success.
     * @param outCurveName Optional extracted curve name.
     * @param outSampleIntervalMs Optional extracted playback interval.
     * @param outError Optional error description on failure.
     * @return True if import succeeded and at least one valid point was loaded.
     */
    static bool importJson(const QString &filePath,
                           QVector<SimTelemetryPoint> &outPoints,
                           QString *outCurveName = nullptr,
                           int *outSampleIntervalMs = nullptr,
                           QString *outError = nullptr);

    /**
     * @brief Exports the telemetry curve to a semicolon/comma-separated CSV file.
     * @param filePath Destination file path (.csv).
     * @param points Curve points to serialize.
     * @param outError Optional error description on failure.
     * @return True if export succeeded.
     */
    static bool exportCsv(const QString &filePath,
                          const QVector<SimTelemetryPoint> &points,
                          QString *outError = nullptr);

    /**
     * @brief Imports a telemetry curve from a CSV file (comma or semicolon delimited).
     * @param filePath Source file path (.csv).
     * @param outPoints Populated curve points on success.
     * @param outError Optional error description on failure.
     * @return True if import succeeded and at least one valid point was loaded.
     */
    static bool importCsv(const QString &filePath,
                          QVector<SimTelemetryPoint> &outPoints,
                          QString *outError = nullptr);
};

#endif // SIM_CURVE_MODEL_H
