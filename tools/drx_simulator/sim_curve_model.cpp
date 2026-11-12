/**
 * @file sim_curve_model.cpp
 * @brief Implementation of RF telemetry curve presets and JSON/CSV import-export.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "sim_curve_model.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QtMath>

QVector<SimTelemetryPoint> SimCurveModel::createPreset(PresetType type, int pointCount) {
    const int n = qBound(5, pointCount, 200);
    QVector<SimTelemetryPoint> pts;
    pts.reserve(n);

    for (int i = 0; i < n; ++i) {
        SimTelemetryPoint p;
        const double ratio = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;

        switch (type) {
        case PresetType::NormalStable:
            p.cnDb = 14.50 + 0.15 * qSin(i * 0.4);
            p.powerDbm = -42.00 + 0.25 * qCos(i * 0.35);
            p.signalLock = true;
            p.videoLock = true;
            p.audioLock = true;
            p.ber = "1.0E-7";
            p.note = "Normal";
            break;

        case PresetType::MiddleCnDropFault:
            if (i >= n / 3 && i <= (2 * n) / 3) {
                p.cnDb = 7.40;
                p.powerDbm = -53.50;
                p.signalLock = true;
                p.videoLock = false;
                p.audioLock = false;
                p.ber = "3.5E-3";
                p.note = "Düşük C/N Hatası";
            } else {
                p.cnDb = 14.60;
                p.powerDbm = -41.50;
                p.signalLock = true;
                p.videoLock = true;
                p.audioLock = true;
                p.ber = "1.0E-7";
                p.note = "Normal";
            }
            break;

        case PresetType::BriefSignalLoss:
            if (i >= (n * 4) / 10 && i <= (n * 6) / 10) {
                p.cnDb = 0.00;
                p.powerDbm = -84.00;
                p.signalLock = false;
                p.videoLock = false;
                p.audioLock = false;
                p.ber = "1.0E-1";
                p.note = "Sinyal Kopması";
            } else {
                p.cnDb = 14.40;
                p.powerDbm = -42.50;
                p.signalLock = true;
                p.videoLock = true;
                p.audioLock = true;
                p.ber = "1.0E-7";
                p.note = "Normal";
            }
            break;

        case PresetType::SteppedDecay:
            p.cnDb = 16.0 - ratio * 10.5;
            p.powerDbm = -38.0 - ratio * 28.0;
            p.signalLock = (p.cnDb >= 6.5);
            p.videoLock = (p.cnDb >= 9.0);
            p.audioLock = p.videoLock;
            p.ber = p.videoLock ? "1.2E-7" : (p.signalLock ? "4.8E-4" : "1.0E-1");
            p.note = p.videoLock ? "Normal" : (p.signalLock ? "Zayıf Sinyal" : "Kilit Yok");
            break;
        }

        pts.append(p);
    }

    return pts;
}

bool SimCurveModel::exportJson(const QString &filePath,
                               const QVector<SimTelemetryPoint> &points,
                               const QString &curveName,
                               int sampleIntervalMs,
                               QString *outError) {
    QJsonObject root;
    root["format"] = "NETA_DRX_SIM_CURVE";
    root["version"] = 1;
    root["curveName"] = curveName.isEmpty() ? QStringLiteral("Özel RF Eğrisi") : curveName;
    root["sampleIntervalMs"] = qBound(50, sampleIntervalMs, 10000);
    root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray arr;
    for (int i = 0; i < points.size(); ++i) {
        const SimTelemetryPoint &p = points.at(i);
        QJsonObject obj;
        obj["step"] = i;
        obj["cnDb"] = qRound(p.cnDb * 100.0) / 100.0;
        obj["powerDbm"] = qRound(p.powerDbm * 100.0) / 100.0;
        obj["signalLock"] = p.signalLock;
        obj["videoLock"] = p.videoLock;
        obj["audioLock"] = p.audioLock;
        obj["ber"] = p.ber;
        obj["note"] = p.note;
        arr.append(obj);
    }
    root["points"] = arr;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (outError) *outError = file.errorString();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool SimCurveModel::importJson(const QString &filePath,
                               QVector<SimTelemetryPoint> &outPoints,
                               QString *outCurveName,
                               int *outSampleIntervalMs,
                               QString *outError) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (outError) *outError = file.errorString();
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (outError) *outError = QStringLiteral("Geçersiz JSON dosyası: %1").arg(err.errorString());
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root.value("points").toArray();
    if (arr.isEmpty()) {
        if (outError) *outError = QStringLiteral("JSON içinde 'points' dizisi boş veya bulunamadı.");
        return false;
    }

    QVector<SimTelemetryPoint> loaded;
    loaded.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        QJsonObject o = v.toObject();
        SimTelemetryPoint p;
        p.cnDb       = qBound(0.0, o.value("cnDb").toDouble(14.5), 30.0);
        p.powerDbm   = qBound(-100.0, o.value("powerDbm").toDouble(-42.0), 0.0);
        p.signalLock = o.value("signalLock").toBool(true);
        p.videoLock  = o.value("videoLock").toBool(true);
        p.audioLock  = o.value("audioLock").toBool(true);
        p.ber        = o.value("ber").toString(p.signalLock ? "1.0E-7" : "1.0E-1");
        p.note       = o.value("note").toString("Normal");
        loaded.append(p);
    }

    outPoints = loaded;
    if (outCurveName) {
        *outCurveName = root.value("curveName").toString("İçe Aktarılan Eğri");
    }
    if (outSampleIntervalMs && root.contains("sampleIntervalMs")) {
        *outSampleIntervalMs = qBound(50, root.value("sampleIntervalMs").toInt(250), 10000);
    }
    return true;
}

bool SimCurveModel::exportCsv(const QString &filePath,
                              const QVector<SimTelemetryPoint> &points,
                              QString *outError) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (outError) *outError = file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts.setCodec("UTF-8");
    ts << "Step,CN_dB,Power_dBm,SignalLock,VideoLock,AudioLock,BER,Note\n";
    for (int i = 0; i < points.size(); ++i) {
        const SimTelemetryPoint &p = points.at(i);
        QString cleanNote = p.note;
        cleanNote.replace(',', ';').replace('\n', ' ');
        ts << i << ","
           << QString::number(p.cnDb, 'f', 2) << ","
           << QString::number(p.powerDbm, 'f', 2) << ","
           << (p.signalLock ? 1 : 0) << ","
           << (p.videoLock ? 1 : 0) << ","
           << (p.audioLock ? 1 : 0) << ","
           << p.ber << ","
           << cleanNote << "\n";
    }
    file.close();
    return true;
}

bool SimCurveModel::importCsv(const QString &filePath,
                              QVector<SimTelemetryPoint> &outPoints,
                              QString *outError) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) *outError = file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts.setCodec("UTF-8");

    QVector<SimTelemetryPoint> loaded;
    int lineNo = 0;
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        ++lineNo;
        if (line.isEmpty()) continue;

        // Skip header row
        if (lineNo == 1 && (line.startsWith("Step", Qt::CaseInsensitive) ||
                            line.startsWith("Adim", Qt::CaseInsensitive))) {
            continue;
        }

        const QChar sep = line.contains(';') && !line.contains(',') ? ';' : ',';
        const QStringList cols = line.split(sep);
        if (cols.size() < 3) continue;

        bool okCn = false;
        bool okPow = false;
        const double cn = cols.at(1).trimmed().toDouble(&okCn);
        const double pow = cols.at(2).trimmed().toDouble(&okPow);
        if (!okCn || !okPow) continue;

        SimTelemetryPoint p;
        p.cnDb = qBound(0.0, cn, 30.0);
        p.powerDbm = qBound(-100.0, pow, 0.0);
        p.signalLock = (cols.size() > 3) ? (cols.at(3).trimmed().toInt() != 0) : true;
        p.videoLock  = (cols.size() > 4) ? (cols.at(4).trimmed().toInt() != 0) : true;
        p.audioLock  = (cols.size() > 5) ? (cols.at(5).trimmed().toInt() != 0) : true;
        p.ber        = (cols.size() > 6 && !cols.at(6).trimmed().isEmpty())
                           ? cols.at(6).trimmed()
                           : (p.signalLock ? "1.0E-7" : "1.0E-1");
        p.note       = (cols.size() > 7) ? cols.at(7).trimmed() : QStringLiteral("Normal");
        loaded.append(p);
    }
    file.close();

    if (loaded.isEmpty()) {
        if (outError) *outError = QStringLiteral("CSV dosyasında geçerli telemetry satırı bulunamadı.");
        return false;
    }

    outPoints = loaded;
    return true;
}
