/**
 * @file db_debug_window.h
 * @brief Standalone dedicated GUI tool for MariaDB connection, schema inspection, and Algorithm 2 locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DB_DEBUG_WINDOW_H
#define DB_DEBUG_WINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTabWidget>

/**
 * @class DbDebugWindow
 * @brief Simple and dedicated diagnostic tool for database connectivity, table schema, and distributed locks.
 */
class DbDebugWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DbDebugWindow(QWidget *parent = nullptr);
    ~DbDebugWindow() override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onRefreshTablesClicked();
    void onTableSelected(int row, int col);
    void onRefreshTableDataClicked();
    void onVerifySchemaClicked();
    void onLoadProfilesClicked();
    void onInsertMockRunClicked();
    void onAcquireLockClicked();
    void onReleaseLockClicked();
    void onHeartbeatClicked();
    void onQueryLockClicked();
    void onStartEngineClicked();
    void onStopEngineClicked();
    void onClearLogsClicked();

private:
    void setupUi();
    void appendLog(const QString &msg, const QString &color = "#E0E0E0");
    void updateConnectionBadge(bool connected);
    void updateLockBadge(int lockedByStation, int channelNo);
    void loadTableData(const QString &tableName);

    // Connection Controls
    QLineEdit *m_txtHost = nullptr;
    QSpinBox *m_spinPort = nullptr;
    QLineEdit *m_txtDbName = nullptr;
    QLineEdit *m_txtUser = nullptr;
    QLineEdit *m_txtPass = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnDisconnect = nullptr;
    QPushButton *m_btnStartEngine = nullptr;
    QPushButton *m_btnStopEngine = nullptr;
    QLabel *m_lblConnStatus = nullptr;

    // Schema and Tables
    QTableWidget *m_tableListWidget = nullptr;
    QPushButton *m_btnRefreshTables = nullptr;
    QPushButton *m_btnVerifySchema = nullptr;
    QPushButton *m_btnLoadProfiles = nullptr;
    QPushButton *m_btnInsertMockRun = nullptr;

    // Table Content Viewer
    QLabel *m_lblSelectedTable = nullptr;
    QSpinBox *m_spinRowLimit = nullptr;
    QPushButton *m_btnRefreshTableData = nullptr;
    QTableWidget *m_tableDataWidget = nullptr;
    QString m_currentSelectedTable;

    // Locking Controls (Algorithm 2)
    QSpinBox *m_spinStationId = nullptr;
    QSpinBox *m_spinChannelNo = nullptr;
    QPushButton *m_btnAcquireLock = nullptr;
    QPushButton *m_btnReleaseLock = nullptr;
    QPushButton *m_btnHeartbeat = nullptr;
    QPushButton *m_btnQueryLock = nullptr;
    QLabel *m_lblLockStatus = nullptr;

    // Log Console
    QTextEdit *m_txtLogs = nullptr;
    QPushButton *m_btnClearLogs = nullptr;

    QTimer m_lockPollTimer;
};

#endif // DB_DEBUG_WINDOW_H
