/**
 * @file drx_clock_badge.cpp
 * @brief Implementation of animated DRX clock badge widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "drx_clock_badge.h"
#include <QHBoxLayout>
#include <QEasingCurve>

DrxClockBadge::DrxClockBadge(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_lblClock = new QLabel(this);
    m_lblClock->setAlignment(Qt::AlignCenter);
    m_lblClock->setCursor(Qt::PointingHandCursor);
    m_lblClock->setFixedWidth(168);
    m_lblClock->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    lay->addWidget(m_lblClock);

    // Setup opacity effect for pulse animation
    m_opacityEffect = new QGraphicsOpacityEffect(m_lblClock);
    m_lblClock->setGraphicsEffect(m_opacityEffect);

    m_fadeAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnim->setDuration(280);
    m_fadeAnim->setStartValue(0.35);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    // Setup flash highlight timer
    m_flashResetTimer.setSingleShot(true);
    connect(&m_flashResetTimer, &QTimer::timeout, this, &DrxClockBadge::onFlashTimeout);

    resetClock();
}

void DrxClockBadge::updateTime(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr) {
    Q_UNUSED(deviceTime);
    m_isConnected = true;

    QString cleanRaw = rawTimeStr.trimmed();
    if (cleanRaw.isEmpty() || cleanRaw == "?" || cleanRaw == "-") {
        return;
    }

    // Determine clean time string for badge display
    QString displayTime = cleanRaw;
    if (cleanRaw.contains(" - ")) {
        // "09:11:45 - 22/09/2026" -> show "09:11:45"
        displayTime = cleanRaw.section(" - ", 0, 0).trimmed();
    }

    // Build informative tooltip with both DRX device time and system instant time
    QString toolTipStr = QString("🛰️ DRX9000 Cihaz Saati: %1\n"
                                 "💻 Sistem Anlık Saati: %2")
                             .arg(cleanRaw)
                             .arg(hostTime.isValid() ? hostTime.toString("dd.MM.yyyy hh:mm:ss.zzz") : QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss.zzz"));
    m_lblClock->setToolTip(toolTipStr);

    // Animate only when time display ticks/changes
    if (displayTime != m_lastTimeStr) {
        m_lastTimeStr = displayTime;
        m_lblClock->setText(QString("🕒 DRX: %1").arg(displayTime));
        triggerTickAnimation();
    }
}

void DrxClockBadge::setConnected(bool connected) {
    m_isConnected = connected;
    if (!connected) {
        resetClock();
    } else if (m_lastTimeStr.isEmpty()) {
        m_lblClock->setText("🕒 DRX: Bağlandı");
        m_lblClock->setStyleSheet(
            "background-color: #064E3B; border: 1.5px solid #10B981; color: #34D399; "
            "padding: 4px 8px; border-radius: 6px; font-weight: bold; font-size: 9pt; "
            "font-family: 'Consolas', 'Courier New', monospace;");
    }
}

void DrxClockBadge::resetClock() {
    m_lastTimeStr.clear();
    m_isConnected = false;
    m_lblClock->setText("🕒 DRX: Bekleniyor");
    m_lblClock->setToolTip("DRX9000 alıcısı henüz bağlı değil.");
    m_lblClock->setStyleSheet(
        "background-color: #181825; border: 1.5px solid #45475A; color: #9399B2; "
        "padding: 4px 8px; border-radius: 6px; font-weight: bold; font-size: 9pt; "
        "font-family: 'Consolas', 'Courier New', monospace;");
}

void DrxClockBadge::triggerTickAnimation() {
    // 1. High-tech pulse styling: bright cyan border and white highlight
    m_lblClock->setStyleSheet(
        "background-color: #065F46; border: 1.5px solid #38BDF8; color: #FFFFFF; "
        "padding: 4px 8px; border-radius: 6px; font-weight: bold; font-size: 9pt; "
        "font-family: 'Consolas', 'Courier New', monospace;");

    // 2. Opacity spring animation
    if (m_fadeAnim) {
        m_fadeAnim->stop();
        m_fadeAnim->start();
    }

    // 3. Reset highlight border back to stable emerald after 220ms
    m_flashResetTimer.start(220);
}

void DrxClockBadge::onFlashTimeout() {
    if (!m_isConnected) return;
    m_lblClock->setStyleSheet(
        "background-color: #064E3B; border: 1.5px solid #10B981; color: #34D399; "
        "padding: 4px 8px; border-radius: 6px; font-weight: bold; font-size: 9pt; "
        "font-family: 'Consolas', 'Courier New', monospace;");
}
