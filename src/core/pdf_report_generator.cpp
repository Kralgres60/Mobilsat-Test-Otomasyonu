/**
 * @file pdf_report_generator.cpp
 * @brief High-precision single-page PDF Quality Control (QC) test certificate generator.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "pdf_report_generator.h"
#include "app_config.h"
#include <QPrinter>
#include <QTextDocument>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QRegExp>

#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#endif

QString PdfReportGenerator::defaultCertificatePath(const QString &antennaSn, qint64 runId) {
    QString baseDir = AppConfig::instance().reportDir();
    if (baseDir.isEmpty()) {
        baseDir = QCoreApplication::applicationDirPath() + "/reports/certificates";
    } else {
        baseDir += "/certificates";
    }
    QDir().mkpath(baseDir);

    QString snClean = antennaSn.trimmed();
    if (snClean.isEmpty()) snClean = "ANTENNA";
    snClean.replace(QRegExp("[^a-zA-Z0-9_-]"), "_");

    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    return QString("%1/NETA_QC_Certificate_%2_TR%3_%4.pdf")
        .arg(baseDir, snClean, QString::number(runId), timeStr);
}

bool PdfReportGenerator::generateCertificate(const TestRunData &run,
                                            const QVector<ChannelSampleResult> &channels,
                                            const QString &outputPath,
                                            QString *outError) {
    if (outputPath.isEmpty()) {
        if (outError) *outError = "Çıktı dosya yolu boş olamaz.";
        return false;
    }

    QFileInfo fi(outputPath);
    QDir().mkpath(fi.absolutePath());

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outputPath);

#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(8, 8, 8, 8), QPageLayout::Millimeter);
#else
    printer.setPaperSize(QPrinter::A4);
    printer.setOrientation(QPrinter::Portrait);
    printer.setPageMargins(8, 8, 8, 8, QPrinter::Millimeter);
#endif

    QString html = buildCertificateHtml(run, channels);

    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setHtml(html);
    doc.print(&printer);

    qInfo() << "[PdfReportGenerator] QC Test Certificate created successfully:" << outputPath;
    return true;
}

QString PdfReportGenerator::buildCertificateHtml(const TestRunData &run,
                                                const QVector<ChannelSampleResult> &channels) {
    const bool isPass = (run.overallResult.trimmed().toUpper() == "PASS");
    const QString verdictText = isPass ? "✔ KALİTE ONAYI VERİLDİ - PASS" :
                                (run.overallResult.trimmed().toUpper() == "ABORTED" ? "⏹ TEST İPTAL EDİLDİ - ABORTED" : "✖ KALİTE STANDARDI DIŞINDA - FAIL");
    const QString verdictBg = isPass ? "#ecfdf5" : (run.overallResult.trimmed().toUpper() == "ABORTED" ? "#fffbeb" : "#fef2f2");
    const QString verdictBorder = isPass ? "#059669" : (run.overallResult.trimmed().toUpper() == "ABORTED" ? "#d97706" : "#dc2626");
    const QString verdictFg = isPass ? "#047857" : (run.overallResult.trimmed().toUpper() == "ABORTED" ? "#b45309" : "#b91c1c");

    QString timeFormatted = run.timestamp.isValid() ? run.timestamp.toString("yyyy-MM-dd HH:mm:ss") : "--";
    QString endTimeFormatted = run.testEndTime.isValid() ? run.testEndTime.toString("yyyy-MM-dd HH:mm:ss") : "--";
    QString drxTime = run.lastDrxDeviceTime.isEmpty() ? "N/A" : run.lastDrxDeviceTime;

    QString html;
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'/>";
    html += "<style>";
    html += "body { font-family: 'Segoe UI', Arial, sans-serif; font-size: 8.5pt; color: #1e293b; line-height: 1.25; margin: 0; padding: 0; }";
    html += "table { width: 100%; border-collapse: collapse; margin-bottom: 6px; }";
    html += "th, td { padding: 4px 6px; text-align: left; }";
    html += ".header-table { border-bottom: 2px solid #059669; padding-bottom: 6px; margin-bottom: 8px; }";
    html += ".title-main { font-size: 13pt; font-weight: bold; color: #047857; margin: 0; }";
    html += ".title-sub { font-size: 8.5pt; color: #475569; font-weight: normal; margin: 0; }";
    html += ".doc-code { font-size: 8pt; color: #64748b; text-align: right; }";
    html += ".info-table { border: 1px solid #cbd5e1; background-color: #f8fafc; margin-bottom: 8px; }";
    html += ".info-table th { background-color: #f1f5f9; color: #334155; font-size: 7.5pt; text-transform: uppercase; border: 1px solid #cbd5e1; }";
    html += ".info-table td { border: 1px solid #cbd5e1; font-size: 8.5pt; }";
    html += ".telemetry-box { background-color: #eff6ff; border: 1px solid #bfdbfe; padding: 4px 8px; border-radius: 4px; margin-bottom: 8px; font-size: 8pt; color: #1e40af; }";
    html += ".data-table { border: 1px solid #cbd5e1; margin-bottom: 8px; }";
    html += ".data-table th { background-color: #0f172a; color: #ffffff; font-size: 7.5pt; font-weight: bold; text-align: center; border: 1px solid #334155; padding: 4px; }";
    html += ".data-table td { border: 1px solid #e2e8f0; font-size: 8pt; text-align: center; padding: 3px 4px; }";
    html += ".badge-pass { background-color: #dcfce7; color: #15803d; font-weight: bold; padding: 1px 4px; border-radius: 2px; }";
    html += ".badge-fail { background-color: #fee2e2; color: #b91c1c; font-weight: bold; padding: 1px 4px; border-radius: 2px; }";
    html += ".badge-warn { background-color: #fef3c7; color: #b45309; font-weight: bold; padding: 1px 4px; border-radius: 2px; }";
    html += ".verdict-banner { border: 2px solid " + verdictBorder + "; background-color: " + verdictBg + "; padding: 8px; text-align: center; border-radius: 4px; margin-bottom: 8px; }";
    html += ".sign-table { border: 1px solid #cbd5e1; margin-top: 6px; }";
    html += ".sign-table td { border: 1px solid #cbd5e1; height: 50px; vertical-align: top; padding: 6px; font-size: 8pt; }";
    html += ".footer-note { font-size: 7pt; color: #94a3b8; text-align: center; margin-top: 6px; }";
    html += "</style></head><body>";

    // 1. Corporate Header
    html += "<table class='header-table'><tr>";
    html += "<td style='width: 65%;'>";
    html += "<div class='title-main'>NETA ELEKTRONİK A.Ş.</div>";
    html += "<div class='title-sub'>ANTEN VE MİKRODALGA SİSTEMLERİ FABRİKASI</div>";
    html += "<div style='font-size: 10pt; font-weight: bold; color: #0f172a; margin-top: 3px;'>KALİTE KONTROL VE FABRİKA KABUL TEST SERTİFİKASI</div>";
    html += "<div style='font-size: 7.5pt; color: #64748b;'>QUALITY CONTROL & FACTORY ACCEPTANCE TEST CERTIFICATE</div>";
    html += "</td>";
    html += "<td class='doc-code' style='width: 35%;'>";
    html += "<b>Belge No:</b> NETA-QC-TR" + QString::number(run.id) + "<br/>";
    html += "<b>Düzenlenme:</b> " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") + "<br/>";
    html += "<b>Test İstasyonu:</b> İstasyon " + QString::number(run.stationId) + "<br/>";
    html += "<b>Standart:</b> NETA-STD-RF-9000";
    html += "</td></tr></table>";

    // 2. Product and Test Identification Grid
    html += "<table class='info-table'>";
    html += "<tr>";
    html += "<th>Anten Seri Numarası (S/N)</th>";
    html += "<th>Ürün Modeli / Kod</th>";
    html += "<th>İş Emri Numarası</th>";
    html += "<th>Uydu Test Profili</th>";
    html += "<th>Test Operatörü</th>";
    html += "</tr><tr>";
    html += "<td><b style='font-size: 10pt; color: #047857;'>" + (run.antennaSn.isEmpty() ? "--" : run.antennaSn) + "</b></td>";
    html += "<td>DRX9000-ANT</td>";
    html += "<td>" + (run.workOrderId > 0 ? QString("İş Emri #%1").arg(run.workOrderId) : "Bağımsız Test") + "</td>";
    html += "<td>" + (run.profileName.isEmpty() ? "Türksat 42.0E" : run.profileName) + "</td>";
    html += "<td>" + (run.testerName.isEmpty() ? (run.operatorUsername.isEmpty() ? "Operatör" : run.operatorUsername) : run.testerName) + "</td>";
    html += "</tr></table>";

    // 3. Dual Time Telemetry Banner
    html += "<div class='telemetry-box'>";
    html += "⏱️ <b>Zaman Telemetrisi:</b> Test Başlangıç: <b>" + timeFormatted + "</b> | Bitiş: <b>" + endTimeFormatted + "</b> ";
    html += "| Donanım (DRX) Saati: <b>" + drxTime + "</b> | Toplam Süre: <b>" + QString::number(run.durationSec) + " sn</b>";
    html += "</div>";

    // 4. 10 Transponder Channel Table
    html += "<table class='data-table'>";
    html += "<thead><tr>";
    html += "<th>CH</th>";
    html += "<th style='text-align: left;'>Kanal Adı</th>";
    html += "<th>Frekans</th>";
    html += "<th>SR</th>";
    html += "<th>Pol</th>";
    html += "<th>DUT C/N</th>";
    html += "<th>REF C/N</th>";
    html += "<th>Fark (Δ)</th>";
    html += "<th>Kilit</th>";
    html += "<th>Tolerans</th>";
    html += "<th>Sonuç</th>";
    html += "</tr></thead><tbody>";

    int chIdx = 1;
    for (const auto &ch : channels) {
        QString sign = ch.deltaDb > 0 ? "+" : "";
        QString badgeClass = (ch.status == "PASS") ? "badge-pass" : ((ch.status == "WARNING") ? "badge-warn" : "badge-fail");
        QString lockStr = QString("%1 %2").arg(ch.carrierLocked ? "S:+" : "S:-").arg(ch.videoLocked ? "V:+" : "V:-");

        html += "<tr>";
        html += QString("<td><b>%1</b></td>").arg(ch.channelNo > 0 ? ch.channelNo : chIdx);
        html += QString("<td style='text-align: left;'><b>%1</b></td>").arg(ch.channelName);
        html += QString("<td>%1 MHz</td>").arg(ch.frequencyMhz);
        html += QString("<td>%1</td>").arg(ch.symbolRate);
        html += QString("<td>%1</td>").arg(ch.polarization);
        html += QString("<td>%1 dB</td>").arg(ch.dutCn, 0, 'f', 1);
        html += QString("<td>%1 dB</td>").arg(ch.refCn, 0, 'f', 1);
        html += QString("<td><b>%1%2 dB</b></td>").arg(sign).arg(ch.deltaDb, 0, 'f', 2);
        html += QString("<td>%1</td>").arg(lockStr);
        html += QString("<td>±%1 dB</td>").arg(ch.toleranceDb, 0, 'f', 2);
        html += QString("<td><span class='%1'>%2</span></td>").arg(badgeClass, ch.status);
        html += "</tr>";
        chIdx++;
    }

    if (channels.isEmpty()) {
        html += "<tr><td colspan='11' style='text-align: center; color: #94a3b8; padding: 10px;'>Kanal detay verisi bulunamadı.</td></tr>";
    }

    html += "</tbody></table>";

    // 5. Verdict & Quality Seal Banner
    html += "<div class='verdict-banner'>";
    html += "<div style='font-size: 13pt; font-weight: bold; color: " + verdictFg + ";'>" + verdictText + "</div>";
    if (isPass) {
        html += "<div style='font-size: 8pt; color: #065f46; margin-top: 2px;'>Bu anten, NETA Elektronik üretim ve fabrika kabul tolerans limitlerine tam uygunluk göstermiştir.</div>";
    } else if (!run.failReason.isEmpty()) {
        html += "<div style='font-size: 8pt; color: #991b1b; margin-top: 2px;'><b>Red Nedeni:</b> " + run.failReason + "</div>";
    }
    html += "<div style='font-size: 8pt; color: #334155; margin-top: 4px;'>";
    html += "<b>Ortalama Fark (Δ):</b> " + QString::number(run.avgDeltaDb, 'f', 2) + " dB &nbsp;&nbsp;|&nbsp;&nbsp; ";
    html += "<b>Maksimum Sapma:</b> " + QString::number(run.maxDeltaDb, 'f', 2) + " dB &nbsp;&nbsp;|&nbsp;&nbsp; ";
    html += "<b>Test Edilen Transponder:</b> " + QString::number(channels.size()) + " Kanal";
    html += "</div></div>";

    // 6. Sign-off / Verification Boxes
    html += "<table class='sign-table'>";
    html += "<tr>";
    html += "<td style='width: 50%;'>";
    html += "<b>TEST EDEN OPERATÖR / TEKNİSYEN</b><br/>";
    html += "Adı Soyadı: " + (run.testerName.isEmpty() ? "____________________" : run.testerName) + "<br/>";
    html += "Tarih: " + timeFormatted + "<br/>";
    html += "İmza: __________________________";
    html += "</td>";
    html += "<td style='width: 50%;'>";
    html += "<b>KALİTE GÜVENCE & TEST MÜHENDİSİ</b><br/>";
    html += "Adı Soyadı: NETA Fabrika Kalite Güvence<br/>";
    html += "Tarih: " + QDateTime::currentDateTime().toString("yyyy-MM-dd") + "<br/>";
    html += "Onay Mührü: <b>[ " + (isPass ? "UYGUN - ONAYLANDI" : "REDDEDİLDİ") + " ]</b>";
    html += "</td>";
    html += "</tr></table>";

    // 7. Footer
    html += "<div class='footer-note'>";
    html += "Bu belge NETA DRX9000 Test Otomasyon Sistemi tarafından otomatik ve doğrulanmış olarak üretilmiştir. © 2026 NETA Elektronik A.Ş. Tüm hakları saklıdır.";
    html += "</div>";

    html += "</body></html>";
    return html;
}
