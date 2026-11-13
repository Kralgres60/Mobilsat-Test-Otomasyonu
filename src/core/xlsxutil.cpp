#include "xlsxutil.h"

#include <QtGui/private/qzipwriter_p.h>
#include <QtGui/private/qzipreader_p.h>
#include <QXmlStreamReader>
#include <QFileInfo>
#include <QDir>
#include <QRegExp>
#include <QDateTime>

namespace {

QString escapeXml(const QString &in)
{
    QString out = in;
    out.replace('&', "&amp;");
    out.replace('<', "&lt;");
    out.replace('>', "&gt;");
    out.replace('"', "&quot;");
    return out;
}

// "C5" -> column letters "C" -> 0-based column index (A=0, B=1, ... Z=25, AA=26).
// Returns -1 if 'r' attribute is missing (caller uses next column).
int columnFromCellRef(const QString &ref)
{
    int col = 0;
    bool any = false;
    for (QChar ch : ref) {
        if (ch.isLetter()) {
            any = true;
            col = col * 26 + (ch.toUpper().unicode() - 'A' + 1);
        } else {
            break;
        }
    }
    return any ? (col - 1) : -1;
}

QString columnLetters(int col) // 0-index -> "A", "B", ... "Z", "AA", ...
{
    QString s;
    col += 1;
    while (col > 0) {
        int rem = (col - 1) % 26;
        s.prepend(QChar('A' + rem));
        col = (col - 1) / 26;
    }
    return s;
}

QByteArray buildContentTypes()
{
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>");
}

QByteArray buildRootRels()
{
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>");
}

QByteArray buildWorkbookRels()
{
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>");
}

QByteArray buildWorkbookXml(const QString &sheetName)
{
    QString safeName = sheetName.isEmpty() ? QStringLiteral("Sayfa1") : sheetName;
    // Excel sheet name is limited to 31 characters. Invalid chars: : \ / ? * [ ]
    safeName.truncate(31);
    safeName.replace(QRegExp("[:\\\\/\\?\\*\\[\\]]"), "_");
    QString xml = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"%1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>").arg(escapeXml(safeName));
    return xml.toUtf8();
}

QByteArray buildStylesXml()
{
    // 2 styles: index 0 = normal data cells, index 1 = header row with dark background.
    return QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"2\">"
        "<font><b/><sz val=\"13\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"13\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/></font>"
        "</fonts>"
        "<fills count=\"2\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1E293B\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"2\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"1\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyAlignment=\"1\"><alignment vertical=\"center\"/></xf>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>");
}

// Calculates column width based on content length for clean readability.
QString buildColsXml(const QStringList &headers, const QVector<QStringList> &rows)
{
    if (headers.isEmpty()) return QString();

    QVector<int> maxLen(headers.size(), 0);
    for (int c = 0; c < headers.size(); ++c) maxLen[c] = headers.at(c).length();
    for (const QStringList &row : rows) {
        for (int c = 0; c < row.size() && c < maxLen.size(); ++c) {
            maxLen[c] = qMax(maxLen[c], row.at(c).length());
        }
    }

    QString xml = QStringLiteral("<cols>");
    for (int c = 0; c < maxLen.size(); ++c) {
        double width = qBound(14.0, maxLen.at(c) + 8.0, 50.0);
        xml += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>")
                   .arg(c + 1).arg(width, 0, 'f', 2);
    }
    xml += QStringLiteral("</cols>");
    return xml;
}

QByteArray buildSheetXml(const QStringList &headers, const QVector<QStringList> &rows)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        // Freezes the header row (row 1) so column titles stay visible when scrolling.
        "<sheetViews><sheetView workbookViewId=\"0\">"
        "<pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
        "<selection pane=\"bottomLeft\"/>"
        "</sheetView></sheetViews>");

    xml += buildColsXml(headers, rows);
    xml += QStringLiteral("<sheetData>");

    auto appendRow = [&xml](int rowNum, const QStringList &cells, bool isHeader) {
        xml += QStringLiteral("<row r=\"%1\">").arg(rowNum);
        for (int c = 0; c < cells.size(); ++c) {
            xml += QStringLiteral("<c r=\"%1%2\"%3 t=\"inlineStr\"><is><t xml:space=\"preserve\">%4</t></is></c>")
                       .arg(columnLetters(c)).arg(rowNum)
                       .arg(isHeader ? QStringLiteral(" s=\"1\"") : QString())
                       .arg(escapeXml(cells.at(c)));
        }
        xml += QStringLiteral("</row>");
    };

    appendRow(1, headers, true);
    for (int r = 0; r < rows.size(); ++r) {
        appendRow(r + 2, rows.at(r), false);
    }

    xml += QStringLiteral("</sheetData></worksheet>");
    return xml.toUtf8();
}

} // namespace

bool writeXlsxSheet(const QString &filePath, const QString &sheetName,
                     const QStringList &headers, const QVector<QStringList> &rows,
                     QString *errorOut)
{
    QFileInfo fi(filePath);
    QDir dir = fi.absoluteDir();
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorOut) *errorOut = QStringLiteral("Klasör oluşturulamadı: %1").arg(dir.absolutePath());
        return false;
    }

    QString tempPath = filePath + QStringLiteral(".tmp_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QFile::remove(tempPath);

    {
        QZipWriter zip(tempPath);
        if (!zip.isWritable()) {
            if (errorOut) *errorOut = QStringLiteral("Dosya yazılabilir değil (geçici dosya açılamadı): %1").arg(tempPath);
            return false;
        }

        zip.addFile(QStringLiteral("[Content_Types].xml"), buildContentTypes());
        zip.addFile(QStringLiteral("_rels/.rels"), buildRootRels());
        zip.addFile(QStringLiteral("xl/workbook.xml"), buildWorkbookXml(sheetName));
        zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), buildWorkbookRels());
        zip.addFile(QStringLiteral("xl/styles.xml"), buildStylesXml());
        zip.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), buildSheetXml(headers, rows));

        zip.close();
        if (zip.status() != QZipWriter::NoError) {
            QFile::remove(tempPath);
            if (errorOut) *errorOut = QStringLiteral("Zip yazım hatası (kod %1): %2").arg(int(zip.status())).arg(filePath);
            return false;
        }
    }

    // Overwrite old file cleanly
    if (QFile::exists(filePath)) {
        QFile::remove(filePath);
    }
    if (!QFile::rename(tempPath, filePath)) {
        // If rename fails, copy and remove temporary file
        QFile::remove(filePath);
        if (!QFile::copy(tempPath, filePath)) {
            if (errorOut) *errorOut = QStringLiteral("Dosya hedefe kaydedilemedi (Excel'de açık olabilir): %1").arg(filePath);
            QFile::remove(tempPath);
            return false;
        }
        QFile::remove(tempPath);
    }

    return true;
}

bool readXlsxSheet(const QString &filePath,
                    QStringList &headersOut, QVector<QStringList> &rowsOut,
                    QString *errorOut)
{
    headersOut.clear();
    rowsOut.clear();

    QZipReader zip(filePath);
    if (!zip.isReadable()) {
        if (errorOut) *errorOut = QStringLiteral("Dosya okunamıyor (geçerli bir .xlsx değil olabilir): %1").arg(filePath);
        return false;
    }

    // Find worksheet XML file (uses first xl/worksheets/*.xml found in archive).
    QString sheetPart;
    const auto entries = zip.fileInfoList();
    for (const auto &info : entries) {
        if (info.isFile && info.filePath.startsWith(QStringLiteral("xl/worksheets/")) && info.filePath.endsWith(".xml")) {
            sheetPart = info.filePath;
            break;
        }
    }
    if (sheetPart.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Excel paketi içinde sayfa (worksheet) bulunamadı: %1").arg(filePath);
        return false;
    }

    // Supports shared string table (xl/sharedStrings.xml) for reading compatibility.
    QStringList sharedStrings;
    QByteArray sharedXml = zip.fileData(QStringLiteral("xl/sharedStrings.xml"));
    if (!sharedXml.isEmpty()) {
        QXmlStreamReader xr(sharedXml);
        QString currentText;
        bool inSi = false;
        while (!xr.atEnd()) {
            xr.readNext();
            if (xr.isStartElement() && xr.name() == QLatin1String("si")) {
                inSi = true;
                currentText.clear();
            } else if (xr.isEndElement() && xr.name() == QLatin1String("si")) {
                inSi = false;
                sharedStrings << currentText;
            } else if (inSi && xr.isCharacters()) {
                // Preserves meaningful whitespace inside <t xml:space="preserve"> tags.
                currentText += xr.text().toString();
            }
        }
    }

    QByteArray sheetXml = zip.fileData(sheetPart);
    if (sheetXml.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Sayfa verisi boş/okunamadı: %1").arg(filePath);
        return false;
    }

    QVector<QStringList> allRows;
    QXmlStreamReader xr(sheetXml);
    QStringList currentRow;
    int currentCol = -1;
    QString cellType;
    QString cellText;
    bool inCellValue = false;

    while (!xr.atEnd()) {
        xr.readNext();
        if (xr.isStartElement()) {
            if (xr.name() == QLatin1String("row")) {
                currentRow.clear();
                currentCol = -1; // her satır A sütunundan yeniden say (bkz. columnFromCellRef fallback)
            } else if (xr.name() == QLatin1String("c")) {
                QString ref = xr.attributes().value("r").toString();
                int col = columnFromCellRef(ref);
                currentCol = (col >= 0) ? col : (currentCol + 1);
                cellType = xr.attributes().value("t").toString();
                cellText.clear();
            } else if (xr.name() == QLatin1String("v") || xr.name() == QLatin1String("t")) {
                inCellValue = true;
            }
        } else if (xr.isCharacters() && inCellValue) {
            cellText += xr.text().toString();
        } else if (xr.isEndElement()) {
            if (xr.name() == QLatin1String("v") || xr.name() == QLatin1String("t")) {
                inCellValue = false;
            } else if (xr.name() == QLatin1String("c")) {
                while (currentRow.size() <= currentCol) currentRow << QString();
                QString value = cellText;
                if (cellType == QLatin1String("s")) {
                    bool ok = false;
                    int idx = value.toInt(&ok);
                    value = (ok && idx >= 0 && idx < sharedStrings.size()) ? sharedStrings.at(idx) : QString();
                }
                currentRow[currentCol] = value;
            } else if (xr.name() == QLatin1String("row")) {
                allRows << currentRow;
            }
        }
    }
    if (xr.hasError()) {
        if (errorOut) *errorOut = QStringLiteral("XML ayrıştırma hatası: %1 (%2)").arg(xr.errorString()).arg(filePath);
        return false;
    }

    if (allRows.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Sayfa boş: %1").arg(filePath);
        return false;
    }

    headersOut = allRows.first();
    rowsOut = allRows.mid(1);
    return true;
}
