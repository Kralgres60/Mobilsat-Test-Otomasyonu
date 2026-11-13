/**
 * @file profile_manager_widget.h
 * @brief Reusable satellite profile, transponder frequency editor, and Golden Sample manager widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef PROFILE_MANAGER_WIDGET_H
#define PROFILE_MANAGER_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QListWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include "core/data_models.h"
#include "hardware/data_transfer.h"

/**
 * @class ProfileManagerWidget
 * @brief Provides satellite profile creation/editing, transponder frequency list management,
 *        and optional Host-side Reference Mode + Golden Sample auto-capture calibration.
 */
class ProfileManagerWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the profile and frequency manager widget.
     * @param isHostMode Enables Administrator Reference Mode switch and live Golden Sample capture
     * @param refWeb Optional Reference DRX Web CGI client (used when isHostMode is true)
     * @param refSerial Optional Reference DRX RS232 worker (used when isHostMode is true)
     * @param parent Parent widget
     */
    explicit ProfileManagerWidget(bool isHostMode = false,
                                  DrxWebClient *refWeb = nullptr,
                                  SerialWorker *refSerial = nullptr,
                                  QWidget *parent = nullptr);

    /** @brief Reloads all active satellite profiles and channels from the database. */
    void reloadProfiles(int selectProfileId = -1);

    /** @brief Syncs the Reference Mode selector UI (Host mode only). */
    void setActiveReferenceMode(ReferenceMode mode);

public slots:
    /** @brief Feeds incoming live Reference DRX telemetry samples during Golden Sample capture. */
    void onReferenceTelemetryReceived(double powDbm, double cnDb);

signals:
    /** @brief Emitted after profiles or channels are saved, created, or deleted. */
    void profilesChanged();
    /** @brief Emitted when Administrator clicks Apply Reference Mode. */
    void referenceModeApplyRequested(ReferenceMode mode);
    /** @brief Emitted for diagnostic and operational logging. */
    void logMessage(const QString &msg);

private slots:
    void onProfileSelected(int row);
    void onNewProfileClicked();
    void onDeleteProfileClicked();
    void onAddChannelRowClicked();
    void onRemoveChannelRowClicked();
    void onSaveProfileClicked();
    void onApplyRefModeClicked();
    void onStartGoldenCaptureClicked();
    void onGoldenCaptureStepTimeout();

private:
    void setupUi();
    void populateChannelRow(int row, const ChannelConfig &ch);
    ChannelConfig readChannelFromRow(int row, double fallbackTol) const;
    void setGoldenCellsOnRow(int row, double cnDb, double powDbm, const QString &statusText, const QColor &statusColor);
    void triggerGoldenCaptureCurrentRow();

    bool m_isHostMode = false;
    DrxWebClient *m_refWebClient = nullptr;
    SerialWorker *m_refSerial = nullptr;

    QComboBox      *m_comboRefMode          = nullptr;
    QListWidget    *m_listProfiles          = nullptr;
    QLineEdit      *m_txtProfName           = nullptr;
    QLineEdit      *m_txtSatName            = nullptr;
    QLineEdit      *m_txtOrbitalPos         = nullptr;
    QDoubleSpinBox *m_spinProfTol           = nullptr;
    QTableWidget   *m_tableProfileChannels  = nullptr;
    QPushButton    *m_btnGoldenCapture      = nullptr;
    QLabel         *m_lblGoldenCaptureStatus = nullptr;

    QVector<SatelliteProfileInfo> m_profiles;
    bool            m_goldenCaptureActive    = false;
    int             m_goldenCaptureProfileId = -1;
    int             m_goldenCaptureRow       = 0;
    QVector<double> m_goldenCnSamples;
    QVector<double> m_goldenPowSamples;
    QTimer          m_goldenStepTimer;
};

#endif // PROFILE_MANAGER_WIDGET_H
