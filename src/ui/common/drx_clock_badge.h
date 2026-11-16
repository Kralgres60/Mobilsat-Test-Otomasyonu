/**
 * @file drx_clock_badge.h
 * @brief Animated top-right status badge displaying real-time DRX hardware clock and latency.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DRX_CLOCK_BADGE_H
#define DRX_CLOCK_BADGE_H

#include <QWidget>
#include <QLabel>
#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTimer>

/**
 * @class DrxClockBadge
 * @brief High-tech animated badge displaying DRX9000 real-time clock with pulse animations on update.
 */
class DrxClockBadge : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Construct a new DrxClockBadge widget.
     * @param parent Optional parent QWidget.
     */
    explicit DrxClockBadge(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~DrxClockBadge() override = default;

public slots:
    /**
     * @brief Update time from DRX alarmstatus packet.
     * @param deviceTime Parsed device timestamp.
     * @param hostTime Arrival host timestamp.
     * @param rawTimeStr Raw string from DRX (e.g. "09:11:45 - 22/09/2026").
     */
    void updateTime(const QDateTime &deviceTime, const QDateTime &hostTime, const QString &rawTimeStr);

    /**
     * @brief Set connection state.
     * @param connected True if receiver is connected and responsive.
     */
    void setConnected(bool connected);

    /**
     * @brief Reset clock display to disconnected state.
     */
    void resetClock();

private slots:
    void onFlashTimeout();

private:
    void triggerTickAnimation();

    QLabel *m_lblClock = nullptr;
    QGraphicsOpacityEffect *m_opacityEffect = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;
    QTimer m_flashResetTimer;

    QString m_lastTimeStr;
    bool m_isConnected = false;
};

#endif // DRX_CLOCK_BADGE_H
