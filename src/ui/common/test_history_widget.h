/**
 * @file test_history_widget.h
 * @brief Shared Test & Operation History widget with Excel (.xlsx) export and import.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TEST_HISTORY_WIDGET_H
#define TEST_HISTORY_WIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include "core/data_models.h"

/**
 * @class TestHistoryWidget
 * @brief Displays Station test runs and Host (Administrator) operations in a unified,
 *        filterable table with bidirectional Excel (.xlsx) export and import support.
 *
 * Designed to be embedded in both HostWindow and ClientWindow left-sidebar navigation.
 */
class TestHistoryWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Test & Operation History widget.
     * @param parent Optional parent widget.
     */
    explicit TestHistoryWidget(QWidget *parent = nullptr);

public slots:
    /**
     * @brief Reloads test run records from the active database and refreshes the table.
     */
    void refreshHistory();

signals:
    /**
     * @brief Emitted when an operation (refresh, export, import) produces a log line.
     * @param msg Informational log message.
     */
    void logMessage(const QString &msg);

private slots:
    /**
     * @brief Applies station and result dropdown filters to the cached run list.
     */
    void applyFilters();

    /**
     * @brief Exports the currently visible table rows to an Excel (.xlsx) workbook.
     */
    void onExportExcelClicked();

    /**
     * @brief Imports test history rows from an Excel (.xlsx) workbook into the database.
     */
    void onImportExcelClicked();

private:
    void setupUi();
    void populateTable(const QVector<TestRunData> &runs);

    QLineEdit    *m_txtSearch     = nullptr;
    QComboBox    *m_comboStation  = nullptr;
    QComboBox    *m_comboResult   = nullptr;
    QPushButton  *m_btnRefresh    = nullptr;
    QPushButton  *m_btnExportXlsx = nullptr;
    QPushButton  *m_btnImportXlsx = nullptr;
    QLabel       *m_lblSummary    = nullptr;
    QTableWidget *m_table         = nullptr;

    QVector<TestRunData> m_cachedRuns;
};

#endif // TEST_HISTORY_WIDGET_H
