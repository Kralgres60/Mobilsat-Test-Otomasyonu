/**
 * @file user_manager_widget.cpp
 * @brief Implementation of the Host-side User & Role Authorization manager widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "user_manager_widget.h"
#include "database/db_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QMessageBox>

UserManagerWidget::UserManagerWidget(QWidget *parent)
    : QWidget(parent) {
    setupUi();
}

void UserManagerWidget::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *grp = new QGroupBox("🔐 Kullanıcı & Yetkilendirme Yönetimi (Mühendis / Tekniker)", this);
    auto *vLay = new QVBoxLayout(grp);
    vLay->setSpacing(8);

    auto *topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    auto *btnAdd = new QPushButton("➕ Yeni Kullanıcı Ekle", grp);
    btnAdd->setCursor(Qt::PointingHandCursor);
    btnAdd->setStyleSheet("background-color: #059669; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(btnAdd, &QPushButton::clicked, this, &UserManagerWidget::onAddUserClicked);

    auto *btnPass = new QPushButton("🔑 Şifre Değiştir", grp);
    btnPass->setCursor(Qt::PointingHandCursor);
    connect(btnPass, &QPushButton::clicked, this, &UserManagerWidget::onChangePasswordClicked);

    auto *btnDel = new QPushButton("🗑️ Seçili Kullanıcıyı Sil", grp);
    btnDel->setCursor(Qt::PointingHandCursor);
    btnDel->setStyleSheet("background-color: #991B1B; color: white; border: none; border-radius: 4px; padding: 6px 14px; font-weight: bold;");
    connect(btnDel, &QPushButton::clicked, this, &UserManagerWidget::onDeleteUserClicked);

    auto *btnRefresh = new QPushButton("🔄 Yenile", grp);
    btnRefresh->setCursor(Qt::PointingHandCursor);
    connect(btnRefresh, &QPushButton::clicked, this, &UserManagerWidget::reloadUsers);

    auto *btnSave = new QPushButton("💾 Yetki Değişikliklerini Kaydet", grp);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet("background-color: #10B981; color: #071210; border: none; border-radius: 4px; padding: 6px 16px; font-weight: bold;");
    connect(btnSave, &QPushButton::clicked, this, &UserManagerWidget::onSaveAllClicked);

    topBar->addWidget(btnAdd);
    topBar->addWidget(btnPass);
    topBar->addWidget(btnDel);
    topBar->addStretch();
    topBar->addWidget(btnRefresh);
    topBar->addWidget(btnSave);
    vLay->addLayout(topBar);

    m_lblStatus = new QLabel(
        "İpucu: ADMIN (Mühendis) tüm ayarlara ve profil yönetimine erişir; OPERATOR (Tekniker) yalnızca test ekranını kullanır.",
        grp);
    m_lblStatus->setWordWrap(true);
    m_lblStatus->setStyleSheet("color: #A7C4B5; font-size: 8.5pt;");
    vLay->addWidget(m_lblStatus);

    m_tableUsers = new QTableWidget(grp);
    m_tableUsers->setColumnCount(5);
    m_tableUsers->setHorizontalHeaderLabels({"ID", "Kullanıcı Adı", "Ad Soyad", "Yetki Rolü", "Aktif"});
    m_tableUsers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableUsers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tableUsers->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tableUsers->verticalHeader()->setVisible(false);
    m_tableUsers->setAlternatingRowColors(true);
    m_tableUsers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableUsers->setSelectionMode(QAbstractItemView::SingleSelection);
    vLay->addWidget(m_tableUsers, 1);

    root->addWidget(grp);
}

void UserManagerWidget::reloadUsers() {
    if (!m_tableUsers) return;

    if (!DbManager::instance().isConnected()) {
        m_tableUsers->setRowCount(0);
        m_lblStatus->setText("🔴 Veritabanı bağlı değil!");
        m_lblStatus->setStyleSheet("color: #F87171; font-weight: bold;");
        return;
    }

    m_users = DbManager::instance().getUsers();
    m_tableUsers->setUpdatesEnabled(false);
    m_tableUsers->setRowCount(m_users.size());

    for (int i = 0; i < m_users.size(); ++i) {
        const UserRecord &u = m_users[i];
        m_tableUsers->setRowHeight(i, 36);

        auto *itId = new QTableWidgetItem(QString::number(u.id));
        itId->setTextAlignment(Qt::AlignCenter);
        itId->setFlags(itId->flags() & ~Qt::ItemIsEditable);
        itId->setData(Qt::UserRole, u.id);
        m_tableUsers->setItem(i, 0, itId);

        auto *itUser = new QTableWidgetItem(u.username);
        if (u.username.compare("admin", Qt::CaseInsensitive) == 0) {
            itUser->setFlags(itUser->flags() & ~Qt::ItemIsEditable);
        }
        m_tableUsers->setItem(i, 1, itUser);
        m_tableUsers->setItem(i, 2, new QTableWidgetItem(u.fullName));

        auto *comboRole = new QComboBox(m_tableUsers);
        comboRole->addItem("ADMIN (Mühendis / Yönetici)", "ADMIN");
        comboRole->addItem("OPERATOR (Tekniker)", "OPERATOR");
        comboRole->setCurrentIndex(u.role == "ADMIN" ? 0 : 1);
        if (u.username.compare("admin", Qt::CaseInsensitive) == 0) {
            comboRole->setEnabled(false);
        }
        m_tableUsers->setCellWidget(i, 3, comboRole);

        auto *chkWrap = new QWidget(m_tableUsers);
        auto *chkLay = new QHBoxLayout(chkWrap);
        chkLay->setContentsMargins(0, 0, 0, 0);
        chkLay->setAlignment(Qt::AlignCenter);
        auto *chkActive = new QCheckBox(chkWrap);
        chkActive->setChecked(u.isActive);
        if (u.username.compare("admin", Qt::CaseInsensitive) == 0) {
            chkActive->setEnabled(false);
        }
        chkLay->addWidget(chkActive);
        m_tableUsers->setCellWidget(i, 4, chkWrap);
    }

    m_tableUsers->setUpdatesEnabled(true);
    m_lblStatus->setText(QString("🟢 Toplam %1 kullanıcı listelendi. ADMIN (Mühendis) / OPERATOR (Tekniker) rollerini değiştirebilirsiniz.")
                             .arg(m_users.size()));
    m_lblStatus->setStyleSheet("color: #34D399; font-size: 8.5pt;");
}

void UserManagerWidget::onAddUserClicked() {
    QDialog dlg(this);
    dlg.setWindowTitle("Yeni Kullanıcı Oluştur");
    dlg.setMinimumWidth(360);

    auto *form = new QFormLayout(&dlg);
    auto *txtUser = new QLineEdit(&dlg);
    txtUser->setPlaceholderText("Örn: muhendis2 veya operator4");
    auto *txtFull = new QLineEdit(&dlg);
    txtFull->setPlaceholderText("Örn: Ahmet Yılmaz");
    auto *txtPass = new QLineEdit("123", &dlg);
    txtPass->setEchoMode(QLineEdit::Password);
    auto *comboRole = new QComboBox(&dlg);
    comboRole->addItem("OPERATOR (Tekniker)", "OPERATOR");
    comboRole->addItem("ADMIN (Mühendis / Yönetici)", "ADMIN");
    auto *chkActive = new QCheckBox("Hesap Aktif", &dlg);
    chkActive->setChecked(true);

    form->addRow("Kullanıcı Adı*:", txtUser);
    form->addRow("Ad Soyad:", txtFull);
    form->addRow("Şifre*:", txtPass);
    form->addRow("Yetki Rolü:", comboRole);
    form->addRow("", chkActive);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString uname = txtUser->text().trimmed();
    const QString pass = txtPass->text();
    if (uname.isEmpty() || pass.isEmpty()) {
        QMessageBox::warning(this, "Eksik Bilgi", "Kullanıcı adı ve şifre boş olamaz.");
        return;
    }

    UserRecord u;
    u.id = 0;
    u.username = uname;
    u.fullName = txtFull->text().trimmed().isEmpty() ? uname : txtFull->text().trimmed();
    u.password = pass;
    u.role = comboRole->currentData().toString();
    u.isActive = chkActive->isChecked();

    if (DbManager::instance().saveUser(u)) {
        emit logMessage(QString("[Yetkilendirme] Yeni kullanıcı eklendi: %1 (%2)").arg(u.username, u.role));
        reloadUsers();
    } else {
        QMessageBox::warning(this, "Hata", "Kullanıcı oluşturulamadı:\n" + DbManager::instance().lastError());
    }
}

void UserManagerWidget::onSaveAllClicked() {
    if (!m_tableUsers) return;
    int saved = 0;
    for (int r = 0; r < m_tableUsers->rowCount(); ++r) {
        UserRecord u;
        u.id = m_tableUsers->item(r, 0) ? m_tableUsers->item(r, 0)->data(Qt::UserRole).toInt() : 0;
        u.username = m_tableUsers->item(r, 1) ? m_tableUsers->item(r, 1)->text().trimmed() : QString();
        u.fullName = m_tableUsers->item(r, 2) ? m_tableUsers->item(r, 2)->text().trimmed() : QString();
        if (auto *combo = qobject_cast<QComboBox*>(m_tableUsers->cellWidget(r, 3))) {
            u.role = combo->currentData().toString();
        }
        if (auto *wrap = m_tableUsers->cellWidget(r, 4)) {
            if (auto *chk = wrap->findChild<QCheckBox*>()) {
                u.isActive = chk->isChecked();
            }
        }
        // Keep password unchanged when saving role/name/active edits
        u.password.clear();

        if (u.id > 0 && DbManager::instance().saveUser(u)) {
            ++saved;
        }
    }

    reloadUsers();
    emit usersUpdated();
    emit logMessage(QString("[Yetkilendirme] %1 kullanıcı yetkisi güncellendi.").arg(saved));
    QMessageBox::information(this, "Kaydedildi", QString("%1 kullanıcı kaydı güncellendi.").arg(saved));
}

void UserManagerWidget::onChangePasswordClicked() {
    int row = m_tableUsers ? m_tableUsers->currentRow() : -1;
    if (row < 0 || row >= m_users.size()) {
        QMessageBox::information(this, "Seçim Yok", "Lütfen şifresini değiştirmek istediğiniz kullanıcıyı seçin.");
        return;
    }

    UserRecord u = m_users[row];
    bool ok = false;
    const QString newPass = QInputDialog::getText(
        this,
        "Şifre Değiştir",
        QString("'%1' kullanıcısı için yeni şifre:").arg(u.username),
        QLineEdit::Password,
        QString(),
        &ok);
    if (!ok) return;
    if (newPass.isEmpty()) {
        QMessageBox::warning(this, "Geçersiz Şifre", "Şifre boş olamaz.");
        return;
    }

    u.password = newPass;
    if (DbManager::instance().saveUser(u)) {
        emit logMessage(QString("[Yetkilendirme] '%1' kullanıcısının şifresi güncellendi.").arg(u.username));
        QMessageBox::information(this, "Başarılı", "Kullanıcı şifresi güncellendi.");
        reloadUsers();
    } else {
        QMessageBox::warning(this, "Hata", "Şifre güncellenemedi:\n" + DbManager::instance().lastError());
    }
}

void UserManagerWidget::onDeleteUserClicked() {
    int row = m_tableUsers ? m_tableUsers->currentRow() : -1;
    if (row < 0 || row >= m_users.size()) {
        QMessageBox::information(this, "Seçim Yok", "Lütfen silmek istediğiniz kullanıcıyı seçin.");
        return;
    }

    const UserRecord &u = m_users[row];
    if (u.username.compare("admin", Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, "Korunan Hesap", "Ana 'admin' hesabı silinemez.");
        return;
    }

    if (QMessageBox::question(this, "Kullanıcıyı Sil",
                              QString("'%1' kullanıcısını silmek istediğinize emin misiniz?").arg(u.username))
        != QMessageBox::Yes) {
        return;
    }

    if (DbManager::instance().deleteUser(u.id)) {
        emit logMessage(QString("[Yetkilendirme] Kullanıcı silindi: %1").arg(u.username));
        reloadUsers();
    } else {
        QMessageBox::warning(this, "Hata", "Kullanıcı silinemedi:\n" + DbManager::instance().lastError());
    }
}
