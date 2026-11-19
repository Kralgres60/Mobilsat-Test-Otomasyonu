/**
 * @file golden_reference_widget.h
 * @brief Host-only Reference Mode controller and direct measurement-based Golden Sample manager.
 *        Supports live audio-like recording, direct fixed value entry, and mini-graph curve inspection.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef GOLDEN_REFERENCE_WIDGET_H
#define GOLDEN_REFERENCE_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QVector>
#include "core/data_models.h"
#include "hardware/ethernet_transport.h"
#include "hardware/serial_transport.h"
#include "ui/common/qcustomplot.h"

/**
 * @class GoldenReferenceWidget
 * @brief Administrator panel for switching Reference Mode (Live vs Golden Sample) and managing direct
 *        Golden Reference measurements via live audio-like recording or fixed baseline entry with mini-graph analysis.
 */
class GoldenReferenceWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Host Golden Reference manager.
     * @param refWeb Reference DRX Web CGI client.
     * @param refSerial Optional Reference DRX RS232 worker.
     * @param parent Optional parent widget.
     */
    explicit GoldenReferenceWidget(DrxWebClient *refWeb, SerialWorker *refSerial, QWidget *parent = nullptr);
    ~GoldenReferenceWidget() override = default;

    /** @brief Reloads stored Golden Sample reference records from database. */
    void reloadFrequencies();

    /** @brief Synchronizes the Reference Mode combo selector. */
    void setActiveReferenceMode(ReferenceMode mode);

    /** @brief Returns true while live measurement recording is active. */
    bool isCaptureActive() const { return m_isRecording; }

public slots:
    /** @brief Accumulates incoming live Reference DRX telemetry samples during live recording. */
    void onReferenceTelemetryReceived(double powDbm, double cnDb);

signals:
    /** @brief Emitted when Administrator clicks Apply Reference Mode. */
    void referenceModeApplyRequested(ReferenceMode mode);
    /** @brief Emitted for operational logging. */
    void logMessage(const QString &msg);

private slots:
    /** @brief Applies selected reference mode and active Golden Sample record to stations. */
    void onApplyRefModeClicked();
    /** @brief Starts or stops live telemetry recording from the Reference DRX receiver. */
    void onToggleLiveRecordClicked();
    /** @brief Handles timer ticks during live Golden Sample recording. */
    void onRecordTick();
    /** @brief Adds a new fixed baseline Golden Sample record to the database and table. */
    void onAddFixedMeasurementClicked();
    /** @brief Deletes the currently selected Golden Sample measurement row. */
    void onRemoveSelectedRowClicked();
    /** @brief Updates the mini plot when the user selects a different measurement row. */
    void onTableSelectionChanged();
    /** @brief Displays context menu for deleting or toggling active state on right-click. */
    void showTableContextMenu(const QPoint &pos);

protected:
    /** @brief Handles Delete key, empty-space collapse, and Shift+Scroll vertical zoom. */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void setupMiniPlot();
    /** @brief Automatically saves any inline edit in the given table row to the database. */
    void autoSaveRow(int row);
    /**
     * @brief Sets the active Golden Sample record ID in the database and refreshes the UI.
     * @param recordId Primary key of the Golden Sample record to activate.
     * @param broadcastIfGoldenMode If true and mode is GoldenSample, broadcasts update to stations.
     */
    void setActiveGoldenRecord(int recordId, bool broadcastIfGoldenMode = true);
    /** @brief Updates the top combo box and table row action buttons to highlight the active record. */
    void refreshActiveGoldenUi();
    void updateMiniPlotForRecord(const GoldenFrequencyRecord &rec);
    void populateRow(int row, const GoldenFrequencyRecord &rec);
    GoldenFrequencyRecord readRow(int row) const;

    DrxWebClient *m_refWebClient = nullptr;
    SerialWorker *m_refSerial    = nullptr;

    // Reference Mode Selector
    QComboBox    *m_comboRefMode      = nullptr;
    int           m_activeGoldenId    = 0;

    // Target Transponder Configuration for Recording & Fixed Entry
    QSpinBox     *m_spinTargetFreq      = nullptr;
    QSpinBox     *m_spinTargetSr        = nullptr;
    QComboBox    *m_comboTargetPol      = nullptr;

    // Live Recording Controls ("Ses kaydeder gibi")
    QPushButton  *m_btnLiveRecord       = nullptr;
    QLabel       *m_lblLiveRecordStatus = nullptr;

    // Direct Fixed Value Entry Controls
    QLineEdit      *m_txtFixedLabel = nullptr;
    QDoubleSpinBox *m_spinFixedCn   = nullptr;
    QDoubleSpinBox *m_spinFixedPow  = nullptr;
    QDoubleSpinBox *m_spinFixedTol  = nullptr;
    QPushButton    *m_btnAddFixed   = nullptr;

    // Measurements Table
    QTableWidget *m_tableMeasurements = nullptr;
    QLabel       *m_lblStatus         = nullptr;

    // Visual Mini Graph
    QLabel      *m_lblPlotTitle = nullptr;
    QCustomPlot *m_miniPlot     = nullptr;

    // Recording State
    bool            m_isRecording         = false;
    int             m_recordingElapsedSec = 0;
    QTimer          m_recordTimer;
    QVector<double> m_currentRecCn;
    QVector<double> m_currentRecPow;
    QVector<double> m_currentRecTime;

    QVector<GoldenFrequencyRecord> m_records;
    bool m_isPopulating = false; ///< Guards against auto-save triggers during table reload
};

#endif // GOLDEN_REFERENCE_WIDGET_H
