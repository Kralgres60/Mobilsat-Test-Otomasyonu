/**
 * @file golden_reference_widget.h
 * @brief Host-only Reference Mode controller and frequency-based Golden Sample calibration manager.
 *        Contains NO satellite profile management (profiles belong strictly to Client stations).
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
#include <QTimer>
#include <QVector>
#include "core/data_models.h"
#include "hardware/drx_web_client.h"
#include "hardware/serial_worker.h"

/**
 * @class GoldenReferenceWidget
 * @brief Administrator panel for switching Reference Mode (Live vs Golden Sample) and measuring/storing
 *        per-frequency Golden Sample reference baselines independently of station profiles.
 */
class GoldenReferenceWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the Host Golden Reference manager.
     * @param refWeb Reference DRX Web CGI client
     * @param refSerial Optional Reference DRX RS232 worker
     * @param parent Parent widget
     */
    explicit GoldenReferenceWidget(DrxWebClient *refWeb, SerialWorker *refSerial, QWidget *parent = nullptr);

    /** @brief Reloads stored frequency Golden Sample records from MariaDB. */
    void reloadFrequencies();

    /** @brief Synchronizes the Reference Mode combo selector. */
    void setActiveReferenceMode(ReferenceMode mode);

public slots:
    /** @brief Accumulates live Reference DRX telemetry samples during calibration capture. */
    void onReferenceTelemetryReceived(double powDbm, double cnDb);

signals:
    /** @brief Emitted when Administrator clicks Apply Reference Mode. */
    void referenceModeApplyRequested(ReferenceMode mode);
    /** @brief Emitted for operational logging. */
    void logMessage(const QString &msg);

private slots:
    void onApplyRefModeClicked();
    void onAddRowClicked();
    void onRemoveRowClicked();
    void onSaveAllClicked();
    void onStartCaptureClicked();
    void onCaptureStepTimeout();

private:
    void setupUi();
    void populateRow(int row, const GoldenFrequencyRecord &rec);
    GoldenFrequencyRecord readRow(int row) const;
    void triggerCaptureForCurrentRow();

    DrxWebClient *m_refWebClient = nullptr;
    SerialWorker *m_refSerial = nullptr;

    QComboBox    *m_comboRefMode = nullptr;
    QTableWidget *m_tableFreqs   = nullptr;
    QPushButton  *m_btnCapture   = nullptr;
    QLabel       *m_lblStatus    = nullptr;

    QVector<GoldenFrequencyRecord> m_records;
    bool            m_captureActive = false;
    int             m_captureRow    = 0;
    QVector<double> m_cnSamples;
    QVector<double> m_powSamples;
    QTimer          m_stepTimer;
};

#endif // GOLDEN_REFERENCE_WIDGET_H
