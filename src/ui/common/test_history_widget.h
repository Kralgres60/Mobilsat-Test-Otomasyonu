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
    void onImportExcelClicked();
    void applyDbFilter(const QString &text);
    void onClearAuditLogsClicked();

private:
    void setupUi();
    void setupHistoryTab(QWidget *tab);
    void setupDbViewerTab(QWidget *tab);
    void setupAuditLogTab(QWidget *tab);
    void populateTable(const QVector<TestRunData> &runs);

    QTabWidget   *m_tabs          = nullptr;

    // Tab 1: Test History
    QLineEdit    *m_txtSearch     = nullptr;
    QComboBox    *m_comboStation  = nullptr;
    QComboBox    *m_comboResult   = nullptr;
    QPushButton  *m_btnRefresh    = nullptr;
    QPushButton  *m_btnExportXlsx = nullptr;
    QPushButton  *m_btnImportXlsx = nullptr;
    QLabel       *m_lblSummary    = nullptr;
    QTableWidget *m_table         = nullptr;
    QVector<TestRunData> m_cachedRuns;

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
