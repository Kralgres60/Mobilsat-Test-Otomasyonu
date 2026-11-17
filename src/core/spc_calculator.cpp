/**
 * @file spc_calculator.cpp
 * @brief Statistical Process Control (SPC) mathematical calculation engine for Cp, Cpk and Gaussian distribution.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "spc_calculator.h"
#include <algorithm>

SpcResult SpcCalculator::calculate(const QVector<double> &data, double toleranceDb, int binCount) {
    SpcResult res;
    res.usl = qAbs(toleranceDb) > 0.01 ? qAbs(toleranceDb) : 1.50;
    res.lsl = -res.usl;
    res.target = 0.0;
    res.sampleCount = data.size();

    if (data.size() < 2) {
        res.ratingBadge = "YETERSİZ VERİ";
        res.ratingText = "SPC analizi için en az 2 test kaydı gereklidir.";
        res.ratingColor = "#9CA3AF";
        return res;
    }

    // 1. Calculate Mean (\mu)
    double sum = 0.0;
    double minV = data.first();
    double maxV = data.first();

    for (double val : data) {
        sum += val;
        if (val < minV) minV = val;
        if (val > maxV) maxV = val;
    }

    res.mean = sum / data.size();
    res.minVal = minV;
    res.maxVal = maxV;

    // 2. Calculate Sample Standard Deviation (\sigma)
    double sumSqDiff = 0.0;
    for (double val : data) {
        double diff = val - res.mean;
        sumSqDiff += diff * diff;
    }

    res.stdDev = qSqrt(sumSqDiff / (data.size() - 1));
    res.threeSigmaLower = res.mean - 3.0 * res.stdDev;
    res.threeSigmaUpper = res.mean + 3.0 * res.stdDev;

    // 3. Process Capability Indices (Cp, Cpk)
    if (res.stdDev > 1e-6) {
        res.cp = (res.usl - res.lsl) / (6.0 * res.stdDev);
        res.cpu = (res.usl - res.mean) / (3.0 * res.stdDev);
        res.cpl = (res.mean - res.lsl) / (3.0 * res.stdDev);
        res.cpk = qMin(res.cpu, res.cpl);
    } else {
        res.cp = 99.9;
        res.cpk = 99.9;
    }

    // 4. Quality Rating Assessment
    if (res.cpk >= 1.33) {
        res.ratingBadge = "MÜKEMMEL (Cp/Cpk >= 1.33)";
        res.ratingText = "Süreç 4-Sigma üzerinde yüksek kalitede çalışmaktadır. Hatalı üretim riski < 63 PPM.";
        res.ratingColor = "#10B981";
    } else if (res.cpk >= 1.00) {
        res.ratingBadge = "YETERLİ / İZLENMELİ (1.00 <= Cpk < 1.33)";
        res.ratingText = "Süreç tolerans sınırları içindedir ancak merkez kaymaları izlenmelidir.";
        res.ratingColor = "#F59E0B";
    } else {
        res.ratingBadge = "YETERSİZ / RİSKLİ (Cpk < 1.00)";
        res.ratingText = "Süreç varyansı veya ortalaması tolerans sınırlarını aşmaktadır. Hatalı anten üretim riski yüksektir!";
        res.ratingColor = "#EF4444";
    }

    // 5. Build Histogram Bins
    int bins = qBound(5, binCount, 50);
    double rangeMin = qMin(res.lsl - 0.2, res.minVal - 0.1);
    double rangeMax = qMax(res.usl + 0.2, res.maxVal + 0.1);
    if (rangeMax <= rangeMin) rangeMax = rangeMin + 1.0;

    res.binWidth = (rangeMax - rangeMin) / bins;
    res.binCenters.resize(bins);
    res.binCounts.fill(0, bins);

    for (int i = 0; i < bins; ++i) {
        res.binCenters[i] = rangeMin + (i + 0.5) * res.binWidth;
    }

    for (double val : data) {
        int idx = static_cast<int>((val - rangeMin) / res.binWidth);
        if (idx < 0) idx = 0;
        if (idx >= bins) idx = bins - 1;
        res.binCounts[idx] += 1.0;
    }

    // 6. Generate Smooth Scaled Gaussian Bell Curve
    if (res.stdDev > 1e-6) {
        const int curvePoints = 160;
        const double sqrt2Pi = qSqrt(2.0 * M_PI);
        const double areaScale = data.size() * res.binWidth;

        res.bellCurveX.resize(curvePoints);
        res.bellCurveY.resize(curvePoints);

        double step = (rangeMax - rangeMin) / (curvePoints - 1);
        for (int i = 0; i < curvePoints; ++i) {
            double x = rangeMin + i * step;
            double z = (x - res.mean) / res.stdDev;
            double pdf = (1.0 / (res.stdDev * sqrt2Pi)) * qExp(-0.5 * z * z);
            res.bellCurveX[i] = x;
            res.bellCurveY[i] = pdf * areaScale;
        }
    }

    return res;
}
