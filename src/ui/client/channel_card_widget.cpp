#include "channel_card_widget.h"
#include "ui/common/style_helper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

ChannelCardWidget::ChannelCardWidget(int channelNo, QWidget *parent)
    : QFrame(parent), m_channelNo(channelNo) {
    
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet("QFrame { background-color: #1E232B; border: 1px solid #2F3846; border-radius: 6px; }");

    QVBoxLayout *vbox = new QVBoxLayout(this);
    vbox->setSpacing(3);
    vbox->setContentsMargins(6, 6, 6, 6);

    // Satır 1: Kanal No, İsim ve Frekans
    QHBoxLayout *topRow = new QHBoxLayout();
    topRow->setSpacing(4);
    m_lblTitle = new QLabel(QString("CH %1: --").arg(channelNo), this);
    m_lblTitle->setStyleSheet("font-weight: bold; color: #64B5F6; font-size: 12px;");
    
    m_lblFreq = new QLabel("- MHz", this);
    m_lblFreq->setStyleSheet("color: #9CA3AF; font-size: 11px;");
    m_lblFreq->setAlignment(Qt::AlignRight);

    topRow->addWidget(m_lblTitle);
    topRow->addWidget(m_lblFreq);
    vbox->addLayout(topRow);

    // Satır 2: DUT ve REF C/N Değerleri
    QHBoxLayout *valRow = new QHBoxLayout();
    valRow->setSpacing(4);
    m_lblDutCn = new QLabel("DUT: -- dB", this);
    m_lblDutCn->setStyleSheet("font-size: 13px; font-weight: bold; color: #FFFFFF;");
    
    m_lblRefCn = new QLabel("REF: -- dB", this);
    m_lblRefCn->setStyleSheet("font-size: 12px; color: #B0BEC5;");
    m_lblRefCn->setAlignment(Qt::AlignRight);

    valRow->addWidget(m_lblDutCn);
    valRow->addWidget(m_lblRefCn);
    vbox->addLayout(valRow);

    // Satır 3: Delta Rozeti + Kilit Bayrakları + Durum Rozeti (Tek satırda)
    QHBoxLayout *botRow = new QHBoxLayout();
    botRow->setSpacing(4);

    m_lblDelta = new QLabel("Δ --", this);
    m_lblDelta->setStyleSheet(StyleHelper::getPendingBadgeStyle());
    m_lblDelta->setAlignment(Qt::AlignCenter);

    m_lblLockFlags = new QLabel("[S:-][V:-]", this);
    m_lblLockFlags->setStyleSheet("color: #78909C; font-family: monospace; font-size: 10px;");
    m_lblLockFlags->setAlignment(Qt::AlignCenter);

    m_lblStatusBadge = new QLabel("BEKLEMEDE", this);
    m_lblStatusBadge->setStyleSheet("color: #9CA3AF; font-size: 10px; font-weight: bold;");
    m_lblStatusBadge->setAlignment(Qt::AlignRight);

    botRow->addWidget(m_lblDelta);
    botRow->addWidget(m_lblLockFlags);
    botRow->addWidget(m_lblStatusBadge);
    vbox->addLayout(botRow);

    reset();
}

void ChannelCardWidget::setConfig(const ChannelConfig &config) {
    m_lblTitle->setText(QString("CH %1: %2").arg(config.channelNo).arg(config.channelName));
    m_lblFreq->setText(QString("%1 %2").arg(config.frequencyMhz).arg(config.polarization));
}

void ChannelCardWidget::reset() {
    m_lblDutCn->setText("DUT: -- dB");
    m_lblRefCn->setText("REF: -- dB");
    m_lblDelta->setText("Δ -- dB");
    m_lblDelta->setStyleSheet(StyleHelper::getPendingBadgeStyle());
    m_lblStatusBadge->setText("BEKLEMEDE");
    m_lblStatusBadge->setStyleSheet("color: #9CA3AF; font-size: 11px; font-weight: bold;");
    updateLockBadges(false, false, false);
    setActive(false);
}

void ChannelCardWidget::setActive(bool active) {
    if (active) {
        setStyleSheet("QFrame { background-color: #1A2838; border: 2px solid #2196F3; border-radius: 8px; padding: 6px; }");
        m_lblStatusBadge->setText("TEST EDİLİYOR...");
        m_lblStatusBadge->setStyleSheet("color: #64B5F6; font-size: 11px; font-weight: bold;");
    } else {
        setStyleSheet("QFrame { background-color: #1E232B; border: 1px solid #2F3846; border-radius: 8px; padding: 6px; }");
    }
}

void ChannelCardWidget::updateSample(double dutCn, double /*dutPow*/, double refCn, double /*refPow*/, double deltaDb) {
    m_lblDutCn->setText(QString("DUT: %1 dB").arg(dutCn, 0, 'f', 1));
    m_lblRefCn->setText(QString("REF: %1 dB").arg(refCn, 0, 'f', 1));
    
    QString sign = deltaDb > 0 ? "+" : "";
    m_lblDelta->setText(QString("Δ %1%2 dB").arg(sign).arg(deltaDb, 0, 'f', 1));
    m_lblDelta->setStyleSheet(StyleHelper::getActiveBadgeStyle());
}

void ChannelCardWidget::setResult(const ChannelSampleResult &result) {
    setActive(false);
    m_lblDutCn->setText(QString("DUT: %1 dB").arg(result.dutCn, 0, 'f', 1));
    m_lblRefCn->setText(QString("REF: %1 dB").arg(result.refCn, 0, 'f', 1));

    QString sign = result.deltaDb > 0 ? "+" : "";
    m_lblDelta->setText(QString("Δ %1%2 dB").arg(sign).arg(result.deltaDb, 0, 'f', 1));

    updateLockBadges(result.carrierLocked, result.videoLocked, result.audioLocked);

    if (result.status == "PASS") {
        m_lblDelta->setStyleSheet(StyleHelper::getPassBadgeStyle());
        m_lblStatusBadge->setText("BAŞARILI (PASS)");
        m_lblStatusBadge->setStyleSheet("color: #4CAF50; font-size: 11px; font-weight: bold;");
        setStyleSheet("QFrame { background-color: #1B2E23; border: 1px solid #2E7D32; border-radius: 8px; padding: 6px; }");
    } else if (result.status == "WARNING") {
        m_lblDelta->setStyleSheet(StyleHelper::getWarningBadgeStyle());
        m_lblStatusBadge->setText("UYARI (V: YOK)");
        m_lblStatusBadge->setStyleSheet("color: #FFA726; font-size: 11px; font-weight: bold;");
        setStyleSheet("QFrame { background-color: #2D2418; border: 1px solid #EF6C00; border-radius: 8px; padding: 6px; }");
    } else {
        m_lblDelta->setStyleSheet(StyleHelper::getFailBadgeStyle());
        m_lblStatusBadge->setText("BAŞARISIZ (FAIL)");
        m_lblStatusBadge->setStyleSheet("color: #EF5350; font-size: 11px; font-weight: bold;");
        setStyleSheet("QFrame { background-color: #2E1B1B; border: 1px solid #C62828; border-radius: 8px; padding: 6px; }");
    }
}

void ChannelCardWidget::updateLockBadges(bool sLock, bool vLock, bool aLock) {
    QString sStr = sLock ? "<font color='#4CAF50'>[S:+]</font>" : "<font color='#EF5350'>[S:-]</font>";
    QString vStr = vLock ? "<font color='#4CAF50'>[V:+]</font>" : "<font color='#FFA726'>[V:-]</font>";
    QString aStr = aLock ? "<font color='#4CAF50'>[A:+]</font>" : "<font color='#9CA3AF'>[A:-]</font>";

    m_lblLockFlags->setText(QString("%1 %2 %3").arg(sStr, vStr, aStr));
}
