/**
 * @file db_debug_widget.h
 * @brief Interactive developer test console for MariaDB connection, schema provisioning, and distributed atomic locking.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DB_DEBUG_WIDGET_H
#define DB_DEBUG_WIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QTableWidget>
#include <QProgressBar>
#include <QTimer>
#include "database/db_manager.h"

/**
 * @class DbDebugWidget
 * @brief Reusable debug UI component for testing MariaDB connectivity, schema creation,
 *        seed data verification, and Algorithm 2 distributed atomic mutual exclusion (reference_lock).
 */
class DbDebugWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the database debug widget.
     * @param parent Parent widget
     */
    explicit DbDebugWidget(QWidget *parent = nullptr);

    /** @brief Destructor. */
    ~DbDebugWidget() override;

private slots:
    /** @brief Connects to MariaDB using provided credentials. */
    void onConnectClicked();

    /** @brief Disconnects active database connection. */
    void onDisconnectClicked();

    /** @brief Starts local portable MariaDB engine. */
    void onStartEngineClicked();

    /** @brief Stops local portable MariaDB engine. */
    void onStopEngineClicked();

    /** @brief Verifies and provisions database schema and default Türksat seed data. */
    void onInitSchemaClicked();

    /** @brief Attempts to acquire atomic distributed reference lock for selected station. */
    void onAcquireLockClicked();

    /** @brief Releases atomic distributed reference lock. */
    void onReleaseLockClicked();

    /** @brief Sends a heartbeat update for holding station. */
    void onHeartbeatClicked();

    /** @brief Queries current lock holder and active channel. */
    void onQueryLockClicked();

    /** @brief Runs automated multi-station contention and deadlock recovery simulation. */
    void onRunContentionSimulationClicked();

    /** @brief Inserts a mock 10-transponder test run into test_runs and test_run_channels. */
    void onInsertMockRunClicked();

    /** @brief Refreshes and displays the latest test runs table. */
    void onRefreshTestRunsClicked();

    /** @brief Loads and displays satellite profiles and 10 transponder channels. */
    void onLoadProfilesClicked();

    /** @brief Starts the automated 6-step full self-test sequence. */
    void onRunFullSuiteClicked();

    /** @brief Step-by-step timer tick handler during full diagnostic suite. */
    void onSuiteTimerTick();

private:
    void setupUi();
    void appendLog(const QString &msg, const QString &color = "#E0E0E0");
    void updateConnectionStatusBadge(bool connected);

    // Database Connection Widgets
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_dbNameEdit = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;
    QPushButton *m_startEngineBtn = nullptr;
    QPushButton *m_stopEngineBtn = nullptr;
    QLabel *m_statusBadge = nullptr;

    // Schema & Seed Widgets
    QPushButton *m_initSchemaBtn = nullptr;
    QPushButton *m_loadProfilesBtn = nullptr;

    // Distributed Locking (reference_lock) Widgets
    QSpinBox *m_stationSpin = nullptr;
    QSpinBox *m_channelSpin = nullptr;
    QPushButton *m_acquireLockBtn = nullptr;
    QPushButton *m_releaseLockBtn = nullptr;
    QPushButton *m_heartbeatBtn = nullptr;
    QPushButton *m_queryLockBtn = nullptr;
    QPushButton *m_contentionSimBtn = nullptr;
    QLabel *m_lockHolderBadge = nullptr;

    // Test Persistence Widgets
    QPushButton *m_insertMockBtn = nullptr;
    QPushButton *m_refreshRunsBtn = nullptr;
    QTableWidget *m_runsTable = nullptr;

    // Log Console
    QTextEdit *m_logEdit = nullptr;

    // Automated Diagnostic Suite Widgets
    QPushButton *m_runFullSuiteBtn = nullptr;
    QProgressBar *m_suiteProgress = nullptr;
    QLabel *m_stepDriverLabel = nullptr;
    QLabel *m_stepLatencyLabel = nullptr;
    QLabel *m_stepSchemaLabel = nullptr;
    QLabel *m_stepChannelsLabel = nullptr;
    QLabel *m_stepLockLabel = nullptr;
    QLabel *m_stepPersistenceLabel = nullptr;
    QLabel *m_suiteResultBanner = nullptr;

    // Timers
    QTimer m_pollTimer;
    QTimer m_suiteTimer;
    int m_suiteStep = 0;
};

#endif // DB_DEBUG_WIDGET_H
