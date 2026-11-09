/**
 * @file xlsxutil.h
 * @brief Zero-dependency minimal OOXML Excel (.xlsx) spreadsheet reader and writer.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 * 
 * Utilizes Qt's private QZipWriter / QZipReader to construct and parse valid single-sheet
 * Office Open XML (.xlsx) workbooks without external dependencies (e.g. QXlsx).
 */

#ifndef XLSXUTIL_H
#define XLSXUTIL_H

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief Writes tabular data (headers and rows) to a valid single-sheet .xlsx spreadsheet.
 * @param filePath Target output .xlsx file path (overwrites if existing)
 * @param sheetName Display name of the worksheet tab
 * @param headers Column header strings
 * @param rows 2D matrix of cell string values
 * @param errorOut Optional pointer to receive error description on failure
 * @return true on success; false on failure
 */
bool writeXlsxSheet(const QString &filePath, const QString &sheetName,
                    const QStringList &headers, const QVector<QStringList> &rows,
                    QString *errorOut = nullptr);

/**
 * @brief Reads tabular data from a simple single-sheet .xlsx spreadsheet.
 * @param filePath Input .xlsx file path
 * @param headersOut Extracted column header strings
 * @param rowsOut Extracted rows of cell values
 * @param errorOut Optional pointer to receive error description on failure
 * @return true on success; false on failure
 */
bool readXlsxSheet(const QString &filePath,
                   QStringList &headersOut, QVector<QStringList> &rowsOut,
                   QString *errorOut = nullptr);

#endif // XLSXUTIL_H
