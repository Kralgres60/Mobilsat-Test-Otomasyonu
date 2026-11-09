/**
 * @file channel_card_widget.h
 * @brief Industrial channel tile widget displaying live RF telemetry and pass/fail badges.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef CHANNEL_CARD_WIDGET_H
#define CHANNEL_CARD_WIDGET_H

#include <QFrame>
#include <QLabel>
#include "data_models.h"

/**
 * @class ChannelCardWidget
 * @brief Visual transponder tile displaying frequency, symbol rate, DUT vs REF C/N,
 *        delta attenuation, carrier/video lock indicators, and PASS/FAIL verdict badges.
 */
class ChannelCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit ChannelCardWidget(int channelNo, QWidget *parent = nullptr);

    void setConfig(const ChannelConfig &config);
    void reset();
    void setActive(bool active);
    void updateSample(double dutCn, double dutPow, double refCn, double refPow, double deltaDb);
    void setResult(const ChannelSampleResult &result);

private:
    void updateLockBadges(bool sLock, bool vLock, bool aLock);

    int m_channelNo;
    QLabel *m_lblTitle;
    QLabel *m_lblFreq;
    QLabel *m_lblDutCn;
    QLabel *m_lblRefCn;
    QLabel *m_lblDelta;
    QLabel *m_lblLockFlags;
    QLabel *m_lblStatusBadge;
};

#endif // CHANNEL_CARD_WIDGET_H
