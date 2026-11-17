/**
 * @file spc_analysis_widget.h
 * @brief Statistical Process Control (SPC) visualization dashboard with Gaussian curve and Cp/Cpk metrics.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SPC_ANALYSIS_WIDGET_H
#define SPC_ANALYSIS_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include "ui/common/qcustomplot.h"
#include "core/spc_calculator.h"

/**
 * @class SpcAnalysisWidget
 * @brief Interactive Statistical Process Control (SPC) inspection dashboard displaying
 *        Delta dB histogram, overlaid Gaussian bell curve, tolerance specification lines,
 *        and real-time Cp / Cpk process capability indices.
 */
class SpcAnalysisWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpcAnalysisWidget(QWidget *parent = nullptr);
    ~SpcAnalysisWidget() override = default;

public slots:
    /** @brief Reloads filter combos from database and recalculates SPC metrics. */
    void refreshAnalysis();

signals:
    void logMessage(const QString &msg);

private slots:
    void onFilterChanged();
    void onExportImageClicked();

private:
    void setupUi();
    void setupPlot();
    void updateKpiDisplay(const SpcResult &spc);
    void renderSpcPlot(const SpcResult &spc);

    // Filters
    QComboBox *m_comboWorkOrders = nullptr;
    QComboBox *m_comboStations   = nullptr;
    QComboBox *m_comboProfiles   = nullptr;
    QComboBox *m_comboLimit      = nullptr;
    QDoubleSpinBox *m_spinTol    = nullptr;
    QPushButton *m_btnRefresh    = nullptr;
    QPushButton *m_btnExportImg  = nullptr;

    // Visualization
    QCustomPlot *m_plot          = nullptr;
    QCPBars     *m_bars          = nullptr;

    // KPI Card Widgets
    QFrame *m_verdictFrame       = nullptr;
    QLabel *m_lblVerdictBadge    = nullptr;
    QLabel *m_lblVerdictText     = nullptr;
    QLabel *m_lblSampleCount     = nullptr;
    QLabel *m_lblMean            = nullptr;
    QLabel *m_lblStdDev          = nullptr;
    QLabel *m_lblCp              = nullptr;
    QLabel *m_lblCpk             = nullptr;
    QLabel *m_lblThreeSigma      = nullptr;
    QLabel *m_lblMinMax          = nullptr;
    QLabel *m_lblToleranceLimits = nullptr;
};

#endif // SPC_ANALYSIS_WIDGET_H
