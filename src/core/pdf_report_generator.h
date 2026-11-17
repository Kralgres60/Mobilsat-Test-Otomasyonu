/**
 * @file pdf_report_generator.h
 * @brief High-precision single-page PDF Quality Control (QC) test certificate generator.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef PDF_REPORT_GENERATOR_H
#define PDF_REPORT_GENERATOR_H

#include <QString>
#include <QVector>
#include "data_models.h"

/**
 * @class PdfReportGenerator
 * @brief Generates one-page vector PDF Quality Control (QC) Factory Acceptance Certificates
 *        for antenna test runs using Qt's printsupport engine.
 */
class PdfReportGenerator {
public:
    /**
     * @brief Generates a one-page A4 PDF quality certificate for the specified test run.
     * @param run Test run summary metadata.
     * @param channels Measurement telemetry for the transponder channels.
     * @param outputPath Target file path for the output PDF.
     * @param outError Optional output string for error description on failure.
     * @return true on success, false on failure.
     */
    static bool generateCertificate(const TestRunData &run,
                                    const QVector<ChannelSampleResult> &channels,
                                    const QString &outputPath,
                                    QString *outError = nullptr);

    /**
     * @brief Returns default output path for an antenna QC certificate.
     * @param antennaSn Antenna serial number.
     * @param runId Database run ID.
     */
    static QString defaultCertificatePath(const QString &antennaSn, qint64 runId = 0);

    /**
     * @brief Builds the HTML document representing the QC certificate.
     * @param run Test run summary metadata.
     * @param channels Measurement telemetry for the transponder channels.
     */
    static QString buildCertificateHtml(const TestRunData &run,
                                        const QVector<ChannelSampleResult> &channels);
};

#endif // PDF_REPORT_GENERATOR_H
