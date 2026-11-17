/**
 * @file test_history_widget.h
 * @brief Unified Test History, SQL Browser, and Audit Trail component with Excel export/import.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef TEST_HISTORY_WIDGET_H
#define TEST_HISTORY_WIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include <QStringList>
#include "core/data_models.h"

/**
 * @class TestHistoryWidget
 * @brief Tab-based component presenting test runs, database tables, and audit logs.
 */
class TestHistoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit TestHistoryWidget(QWidget *parent = nullptr);

public slots:
    void refreshHistory();
    void refreshDbTables();
    void loadSelectedDbTable();
    void refreshAuditLogs();

signals:
    void logMessage(const QString &msg);

private slots:
    void applyFilters();
    void onExportExcelClicked();
    void onExportPdfClicked();
    void onImportExcelClicked();
    void applyDbFilter(const QString &text);
    void onClearAuditLogsClicked();

    // Pagination & Search Slots
    void onFirstPageClicked();
    void onPrevPageClicked();
    void onNextPageClicked();
    void onLastPageClicked();
    void onPageSizeChanged(int index);

private:
    void setupUi();
    void setupHistoryTab(QWidget *tab);
    void setupDbViewerTab(QWidget *tab);
    void setupAuditLogTab(QWidget *tab);
    void populateTable(const QVector<TestRunData> &runs);
    void updatePaginationControls();
    void rebuildBarcodeIndex();

    /**
     * @brief High-performance Binary Search to find a run by ID in O(log N) time.
     * @param runs Sorted slice or array of test runs (sorted descending by ID)
     * @param targetId Target run database ID
     * @return Index if found, or -1 if not found.
     */
    static int binarySearchRunById(const QVector<TestRunData> &runs, qint64 targetId);

    QTabWidget   *m_tabs          = nullptr;

    // Tab 1: Test History
    QLineEdit    *m_txtSearch     = nullptr;
    QComboBox    *m_comboStation  = nullptr;
    QComboBox    *m_comboResult   = nullptr;
    QPushButton  *m_btnRefresh    = nullptr;
    QPushButton  *m_btnExportPdf  = nullptr;
    QPushButton  *m_btnExportXlsx = nullptr;
    QPushButton  *m_btnImportXlsx = nullptr;
    QLabel       *m_lblSummary    = nullptr;
    QTableWidget *m_table         = nullptr;
    QVector<TestRunData> m_cachedRuns;

    // Pagination controls
    QPushButton  *m_btnFirstPage  = nullptr;
    QPushButton  *m_btnPrevPage   = nullptr;
    QLabel       *m_lblPageInfo   = nullptr;
    QPushButton  *m_btnNextPage   = nullptr;
    QPushButton  *m_btnLastPage   = nullptr;
    QComboBox    *m_comboPageSize = nullptr;
    QLabel       *m_lblTotalCount = nullptr;

    int m_currentPage  = 1;
    int m_pageSize     = 50;
    int m_totalRecords = 0;
    int m_totalPages   = 1;

    // Fast In-Memory Barcode Index (O(1) search)
    QHash<QString, QVector<int>> m_barcodeIndex;

    // Tab 2: Database Table Browser
    QComboBox    *m_comboTables   = nullptr;
    QComboBox    *m_comboLimit    = nullptr;
    QLineEdit    *m_txtDbFilter   = nullptr;
    QPushButton  *m_btnDbRefresh  = nullptr;
    QLabel       *m_lblDbInfo     = nullptr;
    QTableWidget *m_tableDb       = nullptr;
    QStringList   m_knownTables;

    // Tab 3: System Event & Audit Logs
    QLineEdit    *m_txtAuditSearch   = nullptr;
    QPushButton  *m_btnAuditRefresh  = nullptr;
    QPushButton  *m_btnAuditClear    = nullptr;
    QLabel       *m_lblAuditSummary  = nullptr;
    QTableWidget *m_tableAudit       = nullptr;
};

#endif // TEST_HISTORY_WIDGET_H
