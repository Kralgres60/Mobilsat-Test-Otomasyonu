/**
 * @file sim_curve_widget.cpp
 * @brief Implementation of the interactive mouse-editable dual-axis RF telemetry graph widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "sim_curve_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>

SimCurveWidget::SimCurveWidget(QWidget *parent)
    : QWidget(parent) {
    setMinimumHeight(250);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

void SimCurveWidget::setCurveData(QVector<SimTelemetryPoint> *points) {
    m_points = points;
    if (m_points && !m_points->isEmpty()) {
        m_selectedIndex = qBound(0, m_selectedIndex, m_points->size() - 1);
        m_playheadIndex = qBound(0, m_playheadIndex, m_points->size() - 1);
    } else {
        m_selectedIndex = -1;
        m_playheadIndex = -1;
    }
    update();
}

void SimCurveWidget::setEditTarget(EditTarget target) {
    m_editTarget = target;
    update();
}

void SimCurveWidget::setSelectedIndex(int index) {
    m_selectedIndex = index;
    update();
}

void SimCurveWidget::setPlayheadIndex(int index) {
    m_playheadIndex = index;
    update();
}

void SimCurveWidget::refresh() {
    update();
}

QRectF SimCurveWidget::plotArea() const {
    const qreal left   = 58.0;
    const qreal right  = 64.0;
    const qreal top    = 32.0;
    const qreal bottom = 32.0;
    return QRectF(left, top, qMax(80.0, width() - left - right), qMax(80.0, height() - top - bottom));
}

int SimCurveWidget::indexFromX(qreal x) const {
    if (!m_points || m_points->isEmpty()) return -1;
    const int n = m_points->size();
    if (n == 1) return 0;

    const QRectF r = plotArea();
    const qreal ratio = qBound(0.0, (x - r.left()) / r.width(), 1.0);
    return qBound(0, qRound(ratio * (n - 1)), n - 1);
}

qreal SimCurveWidget::xFromIndex(int index) const {
    const QRectF r = plotArea();
    if (!m_points || m_points->size() <= 1) return r.left();
    return r.left() + (static_cast<qreal>(index) / (m_points->size() - 1)) * r.width();
}

qreal SimCurveWidget::yFromCn(double cnDb) const {
    const QRectF r = plotArea();
    const double t = qBound(0.0, (cnDb - kMinCn) / (kMaxCn - kMinCn), 1.0);
    return r.bottom() - t * r.height();
}

qreal SimCurveWidget::yFromPower(double powerDbm) const {
    const QRectF r = plotArea();
    const double t = qBound(0.0, (powerDbm - kMinPower) / (kMaxPower - kMinPower), 1.0);
    return r.bottom() - t * r.height();
}

double SimCurveWidget::cnFromY(qreal y) const {
    const QRectF r = plotArea();
    const double t = qBound(0.0, (r.bottom() - y) / r.height(), 1.0);
    const double val = kMinCn + t * (kMaxCn - kMinCn);
    return qRound(val * 10.0) / 10.0;
}

double SimCurveWidget::powerFromY(qreal y) const {
    const QRectF r = plotArea();
    const double t = qBound(0.0, (r.bottom() - y) / r.height(), 1.0);
    const double val = kMinPower + t * (kMaxPower - kMinPower);
    return qRound(val * 10.0) / 10.0;
}

void SimCurveWidget::applyMouseEdit(const QPoint &pos, bool modifyValue) {
    if (!m_points || m_points->isEmpty()) return;
    const QRectF r = plotArea();
    if (!modifyValue && !r.contains(pos)) return;

    const int idx = indexFromX(pos.x());
    if (idx < 0 || idx >= m_points->size()) return;

    if (m_selectedIndex != idx) {
        m_selectedIndex = idx;
        emit selectedIndexChanged(idx);
    }

    if (modifyValue) {
        SimTelemetryPoint &pt = (*m_points)[idx];
        if (m_editTarget == EditTarget::CnCurve) {
            pt.cnDb = cnFromY(pos.y());
            if (pt.cnDb < 9.0 && pt.note == "Normal") {
                pt.note = "Düşük C/N (Manuel)";
            } else if (pt.cnDb >= 9.0 && pt.note == "Düşük C/N (Manuel)") {
                pt.note = "Normal";
            }
        } else {
            pt.powerDbm = powerFromY(pos.y());
        }
        emit pointEdited(idx, pt.cnDb, pt.powerDbm);
    }
    update();
}

void SimCurveWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        applyMouseEdit(event->pos(), true);
    }
    QWidget::mousePressEvent(event);
}

void SimCurveWidget::mouseMoveEvent(QMouseEvent *event) {
    m_hoverIndex = plotArea().contains(event->pos()) ? indexFromX(event->pos().x()) : -1;
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        applyMouseEdit(event->pos(), true);
    } else {
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void SimCurveWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void SimCurveWidget::leaveEvent(QEvent *event) {
    m_hoverIndex = -1;
    update();
    QWidget::leaveEvent(event);
}

void SimCurveWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Background
    p.fillRect(rect(), QColor("#0B1120"));
    const QRectF r = plotArea();

    // Inner chart backdrop
    p.setPen(QPen(QColor("#1E293B"), 1));
    p.setBrush(QColor("#0F172A"));
    p.drawRoundedRect(r, 6, 6);

    // Legend & quick hint header
    QFont headerFont = font();
    headerFont.setPointSize(8);
    headerFont.setBold(true);
    p.setFont(headerFont);

    p.setPen(QColor("#10B981"));
    p.drawText(QRectF(r.left(), 6, 170, 20), Qt::AlignVCenter | Qt::AlignLeft,
               m_editTarget == EditTarget::CnCurve ? "● C/N (dB) [AKTİF ÇİZİM]" : "● C/N (dB)");

    p.setPen(QColor("#38BDF8"));
    p.drawText(QRectF(r.left() + 175, 6, 185, 20), Qt::AlignVCenter | Qt::AlignLeft,
               m_editTarget == EditTarget::PowerCurve ? "● Güç (dBm) [AKTİF ÇİZİM]" : "● Güç (dBm)");

    p.setPen(QColor("#F87171"));
    p.drawText(QRectF(r.left() + 365, 6, 140, 20), Qt::AlignVCenter | Qt::AlignLeft,
               "◆ Hata / Kilit Kaybı");

    p.setPen(QColor("#94A3B8"));
    p.drawText(QRectF(r.left(), 6, r.width(), 20), Qt::AlignVCenter | Qt::AlignRight,
               "Grafik üzerinde sol tık + sürükle ile değer çizebilirsiniz");

    // Horizontal grid lines (5 divisions)
    QFont axisFont = font();
    axisFont.setPointSize(8);
    p.setFont(axisFont);

    const int yDivs = 5;
    for (int g = 0; g <= yDivs; ++g) {
        const qreal frac = static_cast<qreal>(g) / yDivs;
        const qreal y = r.bottom() - frac * r.height();

        p.setPen(QPen(QColor("#1E293B"), 1, Qt::DotLine));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));

        const double cnTick = kMinCn + frac * (kMaxCn - kMinCn);
        p.setPen(QColor("#10B981"));
        p.drawText(QRectF(4, y - 9, r.left() - 10, 18), Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1 dB").arg(cnTick, 0, 'f', 0));

        const double powTick = kMinPower + frac * (kMaxPower - kMinPower);
        p.setPen(QColor("#38BDF8"));
        p.drawText(QRectF(r.right() + 6, y - 9, 54, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1 dBm").arg(powTick, 0, 'f', 0));
    }

    // 9.0 dB typical DVB-S2 lock threshold guide line
    const qreal threshY = yFromCn(9.0);
    p.setPen(QPen(QColor("#F59E0B"), 1, Qt::DashLine));
    p.drawLine(QPointF(r.left(), threshY), QPointF(r.right(), threshY));
    p.setPen(QColor("#FBBF24"));
    p.drawText(QPointF(r.left() + 6, threshY - 4), "C/N Eşik (9.0 dB)");

    if (!m_points || m_points->isEmpty()) {
        p.setPen(QColor("#64748B"));
        p.drawText(r, Qt::AlignCenter, "Veri noktası yok");
        return;
    }

    const int n = m_points->size();

    // Vertical grid & X-axis labels + fault shading bands
    const int labelStride = qMax(1, n / 12);
    const qreal stepW = (n > 1) ? (r.width() / (n - 1)) : r.width();
    for (int i = 0; i < n; ++i) {
        const qreal x = xFromIndex(i);
        if (m_points->at(i).isFaulty()) {
            p.fillRect(QRectF(x - stepW * 0.45, r.top() + 1, stepW * 0.9, r.height() - 2)
                           .intersected(r),
                       QColor(239, 68, 68, 32));
        }

        if (i % labelStride == 0 || i == n - 1) {
            p.setPen(QPen(QColor("#1E293B"), 1, Qt::DotLine));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            p.setPen(QColor("#94A3B8"));
            p.drawText(QRectF(x - 18, r.bottom() + 4, 36, 18), Qt::AlignCenter, QString::number(i));
        }
    }

    // Build Power and C/N paths
    QPainterPath powPath;
    QPainterPath cnPath;
    for (int i = 0; i < n; ++i) {
        const qreal x = xFromIndex(i);
        const qreal yPow = yFromPower(m_points->at(i).powerDbm);
        const qreal yCn  = yFromCn(m_points->at(i).cnDb);
        if (i == 0) {
            powPath.moveTo(x, yPow);
            cnPath.moveTo(x, yCn);
        } else {
            powPath.lineTo(x, yPow);
            cnPath.lineTo(x, yCn);
        }
    }

    // Draw Power curve
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#38BDF8"), m_editTarget == EditTarget::PowerCurve ? 2.6 : 1.7));
    p.drawPath(powPath);

    // Draw C/N curve
    p.setPen(QPen(QColor("#10B981"), m_editTarget == EditTarget::CnCurve ? 2.8 : 1.8));
    p.drawPath(cnPath);

    // Draw nodes
    for (int i = 0; i < n; ++i) {
        const SimTelemetryPoint &pt = m_points->at(i);
        const qreal x = xFromIndex(i);
        const qreal yCn = yFromCn(pt.cnDb);
        const qreal yPow = yFromPower(pt.powerDbm);

        // Power dot
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#38BDF8"));
        p.drawEllipse(QPointF(x, yPow), m_editTarget == EditTarget::PowerCurve ? 3.8 : 2.8,
                      m_editTarget == EditTarget::PowerCurve ? 3.8 : 2.8);

        // C/N dot (or red fault diamond)
        if (pt.isFaulty()) {
            p.setBrush(QColor("#EF4444"));
            p.setPen(QPen(QColor("#FEE2E2"), 1));
            QPolygonF diamond;
            diamond << QPointF(x, yCn - 5.5) << QPointF(x + 5.5, yCn)
                    << QPointF(x, yCn + 5.5) << QPointF(x - 5.5, yCn);
            p.drawPolygon(diamond);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#10B981"));
            p.drawEllipse(QPointF(x, yCn), m_editTarget == EditTarget::CnCurve ? 4.2 : 3.0,
                          m_editTarget == EditTarget::CnCurve ? 4.2 : 3.0);
        }

        // Selected highlight ring
        if (i == m_selectedIndex) {
            const qreal activeY = (m_editTarget == EditTarget::CnCurve) ? yCn : yPow;
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#FFFFFF"), 2.0));
            p.drawEllipse(QPointF(x, activeY), 7.5, 7.5);
        }
    }

    // Active playback cursor
    if (m_playheadIndex >= 0 && m_playheadIndex < n) {
        const qreal px = xFromIndex(m_playheadIndex);
        p.setPen(QPen(QColor("#FBBF24"), 2.0, Qt::DashLine));
        p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));

        QRectF pill(qBound(r.left(), px - 42.0, r.right() - 84.0), r.top() + 4, 84, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#F59E0B"));
        p.drawRoundedRect(pill, 4, 4);
        p.setPen(QColor("#0F172A"));
        QFont pillFont = font();
        pillFont.setPointSize(8);
        pillFont.setBold(true);
        p.setFont(pillFont);
        p.drawText(pill, Qt::AlignCenter, QString("CANLI #%1").arg(m_playheadIndex));
    }

    // Hover readout pill in bottom-right corner inside plot
    const int infoIdx = (m_hoverIndex >= 0 && m_hoverIndex < n) ? m_hoverIndex : m_selectedIndex;
    if (infoIdx >= 0 && infoIdx < n) {
        const SimTelemetryPoint &pt = m_points->at(infoIdx);
        const QString statusStr = pt.signalLock
                                      ? (pt.videoLock ? "KİLİT VAR" : "VİDEO YOK")
                                      : "SİNYAL YOK";
        const QString readout = QString("Adım #%1  |  C/N: %2 dB  |  Güç: %3 dBm  |  %4")
                                    .arg(infoIdx)
                                    .arg(pt.cnDb, 0, 'f', 1)
                                    .arg(pt.powerDbm, 0, 'f', 1)
                                    .arg(statusStr);
        QRectF infoBox(r.right() - 370, r.top() + 6, 362, 22);
        p.setPen(QPen(QColor("#334155"), 1));
        p.setBrush(QColor(15, 23, 42, 220));
        p.drawRoundedRect(infoBox, 4, 4);
        p.setPen(QColor("#E2E8F0"));
        p.drawText(infoBox, Qt::AlignCenter, readout);
    }
}
