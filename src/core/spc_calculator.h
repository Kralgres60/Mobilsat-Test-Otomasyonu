/**
 * @file spc_calculator.h
 * @brief Statistical Process Control (SPC) mathematical calculation engine for Cp, Cpk and Gaussian distribution.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SPC_CALCULATOR_H
#define SPC_CALCULATOR_H

#include <QVector>
#include <QString>
#include <QtMath>

/**
 * @struct SpcResult
 * @brief Contains all calculated SPC capability indices, distribution statistics, and histogram data.
 */
struct SpcResult {
    int sampleCount = 0;            ///< Total number of analyzed samples
    double mean = 0.0;              ///< Sample mean (\mu)
    double stdDev = 0.0;            ///< Sample standard deviation (\sigma)
    double minVal = 0.0;            ///< Observed minimum sample value
    double maxVal = 0.0;            ///< Observed maximum sample value
    double lsl = -1.50;             ///< Lower Specification Limit (dB)
    double usl = 1.50;              ///< Upper Specification Limit (dB)
    double target = 0.0;            ///< Nominal target (0 dB delta)
    double cp = 0.0;                ///< Process Capability Index
    double cpk = 0.0;               ///< Process Capability Performance Index (accounting for centering)
    double cpu = 0.0;               ///< Upper Capability Index
    double cpl = 0.0;               ///< Lower Capability Index
    double threeSigmaLower = 0.0;   ///< \mu - 3\sigma
    double threeSigmaUpper = 0.0;   ///< \mu + 3\sigma

    // Histogram & Curve Data
    double binWidth = 0.1;          ///< Width of each histogram slice
    QVector<double> binCenters;     ///< X coordinates of histogram bin centers
    QVector<double> binCounts;      ///< Frequency of observations in each bin
    QVector<double> bellCurveX;     ///< Fine X grid for smooth Gaussian curve
    QVector<double> bellCurveY;     ///< Scaled Gaussian probability density curve
    QVector<double> cdfCurveY;      ///< Cumulative Distribution Function (CDF %) [0..100]

    // Cumulative Trend & CUSUM Data (1..N in chronological order)
    QVector<double> sampleIndices;       ///< Sample sequence numbers (1, 2, ..., N)
    QVector<double> chronologicalValues; ///< Individual Delta dB values (x_1, ..., x_N)
    QVector<double> cumulativeMeans;     ///< Cumulative Moving Average (\mu_k) at each step k
    QVector<double> cusumValues;         ///< Cumulative Sum of deviations (CUSUM S_k) at each step k
    double finalCusum = 0.0;             ///< Total accumulated deviation at step N (S_N)

    // Quality Rating
    QString ratingBadge;            ///< "EXCELLENT", "CAPABLE", "POOR"
    QString ratingText;             ///< Turkish localized descriptive verdict
    QString ratingColor;            ///< Hex color code for UI badge
};

/**
 * @class SpcCalculator
 * @brief Computes standard deviation, process capability (Cp, Cpk), and Gaussian bell curve coordinates.
 */
class SpcCalculator {
public:
    /**
     * @brief Computes SPC metrics from a collection of continuous measurements (e.g. Delta dB).
     * @param data Array of double precision sample values.
     * @param toleranceDb Tolerance limit in dB (USL = +toleranceDb, LSL = -toleranceDb).
     * @param binCount Number of histogram buckets (default: 15).
     * @return Computed SpcResult structure.
     */
    static SpcResult calculate(const QVector<double> &data, double toleranceDb = 1.50, int binCount = 15);
};

#endif // SPC_CALCULATOR_H
