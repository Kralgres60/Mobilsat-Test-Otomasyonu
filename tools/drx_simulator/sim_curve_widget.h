/**
 * @file sim_curve_widget.h
 * @brief Interactive mouse-editable dual-axis RF telemetry graph widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SIM_CURVE_WIDGET_H
#define SIM_CURVE_WIDGET_H

#include <QWidget>
#include <QVector>
#include "sim_curve_model.h"

/**
 * @class SimCurveWidget
 * @brief Renders C/N (dB) and RF Power (dBm) curves over discrete playback steps
 *        and allows real-time interactive point editing via mouse click and drag.
 */
class SimCurveWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @enum EditTarget
     * @brief Selects which RF parameter vertical mouse dragging modifies.
     */
    enum class EditTarget {
        CnCurve,    ///< Dragging edits C/N in dB [0.0 .. 25.0]
        PowerCurve  ///< Dragging edits RF Power in dBm [-90.0 .. -10.0]
    };

    /**
     * @brief Constructs the interactive RF curve editor canvas.
     * @param parent Optional parent widget.
     */
    explicit SimCurveWidget(QWidget *parent = nullptr);

    /**
     * @brief Replaces the displayed curve points and triggers a repaint.
     * @param points Pointer to external authoritative curve vector.
     */
    void setCurveData(QVector<SimTelemetryPoint> *points);

    /**
     * @brief Selects whether mouse dragging alters the C/N or Power curve.
     * @param target Active curve editing target.
     */
    void setEditTarget(EditTarget target);

    /**
     * @brief Sets the highlighted selected step index.
     * @param index Step index [0 .. N-1] or -1 for none.
     */
    void setSelectedIndex(int index);

    /**
     * @brief Sets the live playback cursor step index.
     * @param index Step index currently transmitted over TCP, or -1 if stopped.
     */
    void setPlayheadIndex(int index);

    /**
     * @brief Triggers an immediate repaint after external table or preset changes.
     */
    void refresh();

signals:
    /**
     * @brief Emitted when the user selects a step on the graph.
     * @param index Selected step index.
     */
    void selectedIndexChanged(int index);

    /**
     * @brief Emitted whenever mouse dragging modifies a step's C/N or Power value.
     * @param index Modified step index.
     * @param cnDb Updated C/N value in dB.
     * @param powerDbm Updated Power value in dBm.
     */
    void pointEdited(int index, double cnDb, double powerDbm);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF plotArea() const;
    int    indexFromX(qreal x) const;
    qreal  xFromIndex(int index) const;
    qreal  yFromCn(double cnDb) const;
    qreal  yFromPower(double powerDbm) const;
    double cnFromY(qreal y) const;
    double powerFromY(qreal y) const;
    void   applyMouseEdit(const QPoint &pos, bool modifyValue);

    QVector<SimTelemetryPoint> *m_points = nullptr;
    EditTarget m_editTarget    = EditTarget::CnCurve;
    int        m_selectedIndex = 0;
    int        m_playheadIndex = 0;
    int        m_hoverIndex    = -1;
    bool       m_dragging      = false;

    static constexpr double kMinCn    = 0.0;
    static constexpr double kMaxCn    = 25.0;
    static constexpr double kMinPower = -90.0;
    static constexpr double kMaxPower = -10.0;
};

#endif // SIM_CURVE_WIDGET_H
