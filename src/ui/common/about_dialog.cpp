#include "about_dialog.h"
#include "core/appinfo.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QPixmap>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
}

void AboutDialog::setupUi()
{
    setWindowTitle(QString("Hakkında - %1").arg(NETA_APP_TITLE));
    setFixedSize(540, 480);
    setStyleSheet(
        "QDialog { background-color: #071210; color: #E6F4EE; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QLabel { color: #E6F4EE; }"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(28, 24, 28, 20);
    mainLayout->setSpacing(14);

    // --- 1. ÜST LOGO & BAŞLIK ALANI ---
    QHBoxLayout *headerLayout = new QHBoxLayout();
    QLabel *lblLogo = new QLabel(this);
    lblLogo->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 4px 10px;");
    QPixmap pix(":/resources/images/logo.png");
    if (pix.isNull()) {
        pix.load(":/resources/images/neta_app.png");
    }
    if (!pix.isNull()) {
        lblLogo->setPixmap(pix.scaledToHeight(40, Qt::SmoothTransformation));
    }

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    QLabel *lblMainTitle = new QLabel(NETA_APP_NAME, this);
    lblMainTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #10B981;");

    QLabel *lblSubTitle = new QLabel("Çoklu İstasyon Eşzamanlı RF Test ve Kalite Otomasyon Sistemi", this);
    lblSubTitle->setStyleSheet("font-size: 11px; color: #34D399;");

    titleLayout->addWidget(lblMainTitle);
    titleLayout->addWidget(lblSubTitle);

    headerLayout->addWidget(lblLogo);
    headerLayout->addSpacing(14);
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // İnce ayırıcı çizgi
    QFrame *line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet("color: #1A3D2B; background-color: #1A3D2B; max-height: 1px;");
    mainLayout->addWidget(line1);

    // --- 2. BİLGİ KART TABLOSU ---
    QFrame *infoCard = new QFrame(this);
    infoCard->setStyleSheet("background-color: #0A1F17; border: 1px solid #1A3D2B; border-radius: 8px; padding: 12px;");
    QVBoxLayout *cardLayout = new QVBoxLayout(infoCard);
    cardLayout->setSpacing(6);

    auto makeRow = [this, cardLayout](const QString &title, const QString &value, const QString &valStyle = "") {
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *lblT = new QLabel(title, this);
        lblT->setFixedWidth(110);
        lblT->setStyleSheet("font-size: 12px; font-weight: bold; color: #34D399;");
        
        QLabel *lblV = new QLabel(value, this);
        lblV->setStyleSheet(valStyle.isEmpty() ? "font-size: 12px; color: #E6F4EE;" : valStyle);
        lblV->setTextInteractionFlags(Qt::TextSelectableByMouse);

        row->addWidget(lblT);
        row->addWidget(lblV);
        row->addStretch();
        cardLayout->addLayout(row);
    };

    makeRow("Sürüm:", QString("%1 (32-bit Release)").arg(NETA_APP_VERSION), "font-size: 12px; font-weight: bold; color: #34D399;");
    makeRow("Geliştirici:", NETA_APP_DEVELOPER, "font-size: 13px; font-weight: bold; color: #FBBF24;");
    makeRow("Kurumsal:", NETA_APP_ORGANIZATION);
    makeRow("Telif Hakkı:", NETA_APP_COPYRIGHT, "font-size: 11px; color: #A7C4B5;");
    makeRow("Web Sitesi:", "<a href='https://www.neta.com.tr' style='color:#34D399; text-decoration:none;'>www.neta.com.tr</a>");

    mainLayout->addWidget(infoCard);

    // --- 3. İTAFEN & MİMARİ AÇIKLAMA METNİ ---
    QLabel *lblDedication = new QLabel(this);
    lblDedication->setWordWrap(true);
    lblDedication->setStyleSheet(
        "background-color: #0A1F17; border-left: 3px solid #10B981; border-radius: 4px; "
        "padding: 10px 14px; font-size: 11.5px; color: #A7C4B5; line-height: 1.5;"
    );
    lblDedication->setText(QString(
        "Bu otomasyon yazılımı; NETA DRX9000 serisi mobil uydu ve alıcı sistemlerinin seri üretim "
        "hattında çoklu istasyon eşzamanlı test, referans almaç karşılaştırmalı canlı RF telemetri analizi, "
        "otomatik taşıyıcı/video kilitleme denetimi ve fabrika merkezi veritabanı kalite kayıt süreçleri için "
        "<b>%1</b> tarafından özel olarak tasarlanıp geliştirilmiştir."
    ).arg(NETA_APP_DEVELOPER));

    mainLayout->addWidget(lblDedication);
    mainLayout->addStretch();

    // --- 4. ALT KAPAT BUTONU ---
    QHBoxLayout *botLayout = new QHBoxLayout();
    QLabel *lblFooterNote = new QLabel(QString("© 2026 %1").arg(NETA_APP_ORGANIZATION), this);
    lblFooterNote->setStyleSheet("font-size: 11px; color: #4B7A66;");

    QPushButton *btnClose = new QPushButton("Kapat", this);
    btnClose->setFixedSize(110, 34);
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setStyleSheet(
        "QPushButton { background-color: #10B981; color: #071210; font-weight: bold; border-radius: 6px; font-size: 12px; }"
        "QPushButton:hover { background-color: #34D399; }"
        "QPushButton:pressed { background-color: #059669; color: #FFFFFF; }"
    );
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    botLayout->addWidget(lblFooterNote);
    botLayout->addStretch();
    botLayout->addWidget(btnClose);
    mainLayout->addLayout(botLayout);
}
