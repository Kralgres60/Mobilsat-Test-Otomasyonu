/**
 * @file simulator_window.h
 * @brief Interactive TCP DRX9000 telemetry curve editor and network transmitter GUI.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SIMULATOR_WINDOW_H
#define SIMULATOR_WINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVector>
#include "drx_sim_server.h"
#include "sim_curve_model.h"
#include "sim_curve_widget.h"

/**
 * @class SimulatorWindow
 * @brief Single-window DRX9000 TCP simulator providing interactive RF graph editing,
 *        step-by-step fault injection, JSON/CSV curve import-export, and LAN-accessible
 *        TCP/HTTP-CGI telemetry streaming for multi-PC testing.
 */
class SimulatorWindow : public QMainWindow {
    Q_OBJECT
public:
    /**
     * @brief Constructs the simulator main window and loads the default stable curve.
     * @param parent Optional parent widget.
     */
    explicit SimulatorWindow(QWidget *parent = nullptr);
    ~SimulatorWindow() override;

private slots:
    void onToggleServer();
    void onPlayPause();
    void onRewindPlayback();
    void onPlaybackTick();
    void onApplyPreset();
    void onAddStep();
    void onRemoveLastStep();
    void onImportCurve();
    void onExportCurve();
    void onCurvePointEdited(int index, double cnDb, double powerDbm);
    void onGraphStepSelected(int index);
    void onTableCellChanged(int row, int col);
    void onTableSelectionChanged();
    void onInjectLowCnFault();
    void onInjectSignalLoss();
    void onInjectVideoLoss();
    void onRestoreNormalPoint();
    void onJumpToSelectedStep();
    void onRequestHandled(quint64 totalCount, const QString &peerIp, const QString &actionTag);
    void onServerTuned(int freqMhz, int symbolRate, const QString &pol);
    void appendLog(const QString &msg);

private:
    void setupUi();
    void populateBindAddresses();
    void refreshEndpointHint();
    void rebuildTable();
    void updateTableRow(int row);
    void syncLiveSampleToServer();
    void setActivePlayIndex(int index);
    QString detectPrimaryLanIp() const;

    DrxSimServer *m_server       = nullptr;
    QTimer       *m_playTimer    = nullptr;
    QVector<SimTelemetryPoint> m_points;
    int           m_playIndex    = 0;
    bool          m_isPlaying    = true;
    bool          m_syncingTable = false;
    QString       m_cachedPrimaryLanIp = QStringLiteral("127.0.0.1");

    // Network Controls
    QComboBox   *m_comboBindIp   = nullptr;
    QSpinBox    *m_spinPort      = nullptr;
    QPushButton *m_btnStartStop  = nullptr;
    QLabel      *m_lblTcpStatus  = nullptr;
    QLabel      *m_lblEndpoint   = nullptr;

    // Curve & Playback Controls
    QPushButton *m_btnEditCn     = nullptr;
    QPushButton *m_btnEditPower  = nullptr;
    QComboBox   *m_comboPreset   = nullptr;
    QPushButton *m_btnPlayPause  = nullptr;
    QSpinBox    *m_spinInterval  = nullptr;
    QCheckBox   *m_chkLoop       = nullptr;
    QCheckBox   *m_chkNoise      = nullptr;

    // Interactive Graph & Step Table
    SimCurveWidget *m_curveWidget = nullptr;
    QTableWidget   *m_table       = nullptr;

    // Live Output & Diagnostics
    QLabel    *m_lblLiveStep   = nullptr;
    QLabel    *m_lblLiveCn     = nullptr;
    QLabel    *m_lblLivePower  = nullptr;
    QLabel    *m_lblLiveLock   = nullptr;
    QLabel    *m_lblLastPeer   = nullptr;
    QLabel    *m_lblTunedInfo  = nullptr;
    QTextEdit *m_txtLog        = nullptr;
};

#endif // SIMULATOR_WINDOW_H
