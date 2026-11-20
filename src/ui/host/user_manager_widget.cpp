/**
 * @file user_manager_widget.cpp
 * @brief Implementation of the Host-side User & Role Authorization manager widget.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "user_manager_widget.h"
#include "core/app_config.h"
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
#include <QMenu>
#include <QAction>
#include <QKeyEvent>

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

    topBar->addWidget(btnAdd);
    topBar->addStretch();
    vLay->addLayout(topBar);

    m_lblStatus = new QLabel(
        "İpucu: Değişiklikler otomatik kaydedilir. Düzenlemek için satıra çift tıklayın; silmek için Sağ Tık veya Delete tuşunu kullanın.",
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
    m_tableUsers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableUsers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableUsers->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tableUsers->installEventFilter(this);
    m_tableUsers->viewport()->installEventFilter(this);
    connect(m_tableUsers, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        onEditUserPopup(row);
    });
    connect(m_tableUsers, &QTableWidget::customContextMenuRequested, this, &UserManagerWidget::showTableContextMenu);
    vLay->addWidget(m_tableUsers, 1);

    root->addWidget(grp);
}

void UserManagerWidget::reloadUsers() {
    if (!m_tableUsers) return;

    m_isPopulating = true;
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

        m_tableUsers->setItem(i, 1, new QTableWidgetItem(u.username));
        m_tableUsers->setItem(i, 2, new QTableWidgetItem(u.fullName));

        const bool isAdmin = (u.role == "ADMIN");
        auto *itRole = new QTableWidgetItem(isAdmin ? "🛡️ ADMIN (Mühendis / Yönetici)" : "🔧 OPERATOR (Tekniker)");
        itRole->setTextAlignment(Qt::AlignCenter);
        itRole->setForeground(QBrush(QColor(isAdmin ? "#FBBF24" : "#38BDF8")));
        if (isAdmin) {
            itRole->setFont(QFont("Segoe UI", 9, QFont::Bold));
        }
        m_tableUsers->setItem(i, 3, itRole);

        auto *chkWrap = new QWidget(m_tableUsers);
        auto *chkLay = new QHBoxLayout(chkWrap);
        chkLay->setContentsMargins(0, 0, 0, 0);
        chkLay->setAlignment(Qt::AlignCenter);
        auto *chkActive = new QCheckBox(chkWrap);
        chkActive->setChecked(u.isActive);
        if (u.username.compare("admin", Qt::CaseInsensitive) == 0) {
            chkActive->setEnabled(false);
        }
        connect(chkActive, &QCheckBox::toggled, this, [this, i](bool) {
            autoSaveRow(i);
        });
        chkLay->addWidget(chkActive);
        m_tableUsers->setCellWidget(i, 4, chkWrap);
    }

    m_tableUsers->setUpdatesEnabled(true);
    m_isPopulating = false;
    if (DbManager::instance().isConnected()) {
        m_lblStatus->setText(QString("🟢 Toplam %1 kullanıcı listelendi (MariaDB + Şifreli Yerel Kasa senkronize).")
                                 .arg(m_users.size()));
        m_lblStatus->setStyleSheet("color: #34D399; font-size: 8.5pt;");
    } else {
        m_lblStatus->setText(QString("🟡 Çevrimdışı Şifreli Kasa Modu (%1 kullanıcı — değişiklikler yerel kasaya ve bağlı istasyonlara uygulanır).")
                                 .arg(m_users.size()));
        m_lblStatus->setStyleSheet("color: #FBBF24; font-size: 8.5pt; font-weight: bold;");
    }
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
        emit usersUpdated();
    } else {
        QMessageBox::warning(this, "Hata", "Kullanıcı oluşturulamadı:\n" + DbManager::instance().lastError());
    }
}

void UserManagerWidget::autoSaveRow(int row) {
    if (m_isPopulating || !m_tableUsers || row < 0 || row >= m_users.size()) return;

    UserRecord u = m_users[row];
    u.id = m_tableUsers->item(row, 0) ? m_tableUsers->item(row, 0)->data(Qt::UserRole).toInt() : u.id;
    u.username = m_tableUsers->item(row, 1) ? m_tableUsers->item(row, 1)->text().trimmed() : u.username;
    u.fullName = m_tableUsers->item(row, 2) ? m_tableUsers->item(row, 2)->text().trimmed() : u.fullName;
    if (auto *wrap = m_tableUsers->cellWidget(row, 4)) {
        if (auto *chk = wrap->findChild<QCheckBox*>()) {
            u.isActive = chk->isChecked();
        }
    }
    u.password.clear(); // Keep password unchanged during inline edits

    if (u.id > 0 && DbManager::instance().saveUser(u)) {
        if (row < m_users.size()) {
            m_users[row] = u;
        }
        emit usersUpdated();
        emit logMessage(QString("[Yetkilendirme] Kullanıcı otomatik güncellendi: %1 (%2, %3)")
                            .arg(u.username, u.role, u.isActive ? "Aktif" : "Pasif"));
        if (m_lblStatus) {
            m_lblStatus->setText(QString("✔ Otomatik kaydedildi: %1 (%2)").arg(u.username, u.role));
            m_lblStatus->setStyleSheet("color: #34D399; font-size: 8.5pt;");
        }
    }
}

void UserManagerWidget::onEditUserPopup(int row) {
    if (row < 0 || row >= m_users.size()) return;
    UserRecord u = m_users[row];
    bool isAdminAccount = (u.username.compare("admin", Qt::CaseInsensitive) == 0);

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Kullanıcı Düzenle: %1").arg(u.username));
    dlg.setMinimumWidth(380);

    auto *form = new QFormLayout(&dlg);
    auto *txtUser = new QLineEdit(u.username, &dlg);
    txtUser->setEnabled(!isAdminAccount);

    auto *txtFull = new QLineEdit(u.fullName, &dlg);
    txtFull->setPlaceholderText("Ad Soyad giriniz");

    auto *txtPass = new QLineEdit(&dlg);
    txtPass->setEchoMode(QLineEdit::Password);
    txtPass->setPlaceholderText("Değiştirmek istemiyorsanız boş bırakın");

    auto *comboRole = new QComboBox(&dlg);
    comboRole->addItem("OPERATOR (Tekniker)", "OPERATOR");
    comboRole->addItem("ADMIN (Mühendis / Yönetici)", "ADMIN");
    comboRole->setCurrentIndex(u.role == "ADMIN" ? 1 : 0);
    comboRole->setEnabled(!isAdminAccount);

    auto *chkActive = new QCheckBox("Hesap Aktif", &dlg);
    chkActive->setChecked(u.isActive);
    chkActive->setEnabled(!isAdminAccount);

    form->addRow("Kullanıcı Adı*:", txtUser);
    form->addRow("Ad Soyad:", txtFull);
    form->addRow("Yeni Şifre:", txtPass);
    form->addRow("Yetki Rolü:", comboRole);
    form->addRow("", chkActive);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString newUser = txtUser->text().trimmed();
    if (newUser.isEmpty()) {
        QMessageBox::warning(this, "Eksik Bilgi", "Kullanıcı adı boş olamaz.");
        return;
    }

    u.username = newUser;
    u.fullName = txtFull->text().trimmed().isEmpty() ? newUser : txtFull->text().trimmed();
    u.password = txtPass->text(); // Empty = keep existing password
    u.role = comboRole->currentData().toString();
    u.isActive = chkActive->isChecked();

    if (DbManager::instance().saveUser(u)) {
        emit logMessage(QString("[Yetkilendirme] Kullanıcı güncellendi: %1 (%2)").arg(u.username, u.role));
        reloadUsers();
        emit usersUpdated();
    } else {
        QMessageBox::warning(this, "Hata", "Kullanıcı güncellenemedi:\n" + DbManager::instance().lastError());
    }
}

void UserManagerWidget::showTableContextMenu(const QPoint &pos) {
    if (!m_tableUsers) return;
    QModelIndex idx = m_tableUsers->indexAt(pos);
    if (!idx.isValid()) return;

    int row = idx.row();
    if (m_tableUsers->selectionModel() &&
        !m_tableUsers->selectionModel()->isRowSelected(row, QModelIndex())) {
        m_tableUsers->selectRow(row);
    }
    if (row < 0 || row >= m_users.size()) return;

    const auto selRows = m_tableUsers->selectionModel()
                             ? m_tableUsers->selectionModel()->selectedRows()
                             : QModelIndexList();
    int selCount = qMax(1, selRows.size());
    bool anyDeletable = false;
    if (selRows.isEmpty()) {
        anyDeletable = (m_users[row].username.compare("admin", Qt::CaseInsensitive) != 0);
    } else {
        for (const QModelIndex &rIdx : selRows) {
            int r = rIdx.row();
            if (r >= 0 && r < m_users.size() && m_users[r].username.compare("admin", Qt::CaseInsensitive) != 0) {
                anyDeletable = true;
                break;
            }
        }
    }

    QMenu menu(this);
    QAction *actEdit = menu.addAction("✏️ Kullanıcıyı Düzenle");
    actEdit->setEnabled(selCount == 1);
    QAction *actPass = menu.addAction("🔑 Şifre Değiştir");
    actPass->setEnabled(selCount == 1);
    menu.addSeparator();
    QAction *actDel = menu.addAction(selCount > 1
                                         ? QString("🗑️ Seçili %1 Kullanıcıyı Sil (Delete)").arg(selCount)
                                         : QString("🗑️ Kullanıcıyı Sil (Delete)"));
    actDel->setEnabled(anyDeletable);

    QAction *chosen = menu.exec(m_tableUsers->viewport()->mapToGlobal(pos));
    if (chosen == actEdit) {
        onEditUserPopup(row);
    } else if (chosen == actPass) {
        onChangePasswordClicked();
    } else if (chosen == actDel) {
        onDeleteUserClicked();
    }
}

bool UserManagerWidget::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && m_tableUsers &&
        (watched == m_tableUsers || watched == m_tableUsers->viewport())) {
        auto *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Delete) {
            onDeleteUserClicked();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
        DbManager::instance().logAudit(
            "USER_PASSWORD_CHANGED",
            QString("Kullanıcı şifresi değiştirildi: %1").arg(u.username),
            AppConfig::instance().currentUsername(), 0);
        emit logMessage(QString("[Yetkilendirme] '%1' kullanıcısının şifresi güncellendi.").arg(u.username));
        QMessageBox::information(this, "Başarılı", "Kullanıcı şifresi güncellendi.");
        reloadUsers();
        emit usersUpdated();
    } else {
        QMessageBox::warning(this, "Hata", "Şifre güncellenemedi:\n" + DbManager::instance().lastError());
    }
}

void UserManagerWidget::onDeleteUserClicked() {
    if (!m_tableUsers) return;
    QVector<UserRecord> toDelete;
    bool skippedAdmin = false;
    if (m_tableUsers->selectionModel()) {
        const auto selRows = m_tableUsers->selectionModel()->selectedRows();
        for (const QModelIndex &rIdx : selRows) {
            int r = rIdx.row();
            if (r >= 0 && r < m_users.size()) {
                if (m_users[r].username.compare("admin", Qt::CaseInsensitive) == 0) {
                    skippedAdmin = true;
                } else {
                    toDelete.append(m_users[r]);
                }
            }
        }
    }
    if (toDelete.isEmpty() && !skippedAdmin) {
        int row = m_tableUsers->currentRow();
        if (row >= 0 && row < m_users.size()) {
            if (m_users[row].username.compare("admin", Qt::CaseInsensitive) == 0) {
                skippedAdmin = true;
            } else {
                toDelete.append(m_users[row]);
            }
        }
    }

    if (toDelete.isEmpty()) {
        if (skippedAdmin) {
            QMessageBox::warning(this, "Korunan Hesap", "Ana 'admin' hesabı silinemez.");
        } else {
            QMessageBox::information(this, "Seçim Yok", "Lütfen silmek istediğiniz kullanıcıyı seçin.");
        }
        return;
    }

    const QString msg = (toDelete.size() == 1)
                            ? QString("'%1' kullanıcısını silmek istediğinize emin misiniz?").arg(toDelete.first().username)
                            : QString("%1 adet seçili kullanıcıyı silmek istediğinize emin misiniz?").arg(toDelete.size());
    if (QMessageBox::question(this, "Kullanıcıyı Sil", msg) != QMessageBox::Yes) {
        return;
    }

    int deleted = 0;
    for (const UserRecord &u : toDelete) {
        if (DbManager::instance().deleteUser(u.id)) {
            DbManager::instance().logAudit(
                "USER_DELETED",
                QString("Kullanıcı silindi: %1").arg(u.username),
                AppConfig::instance().currentUsername(), 0);
            emit logMessage(QString("[Yetkilendirme] Kullanıcı silindi: %1").arg(u.username));
            deleted++;
        }
    }
    if (deleted > 0) {
        reloadUsers();
        emit usersUpdated();
    } else {
        QMessageBox::warning(this, "Hata", "Kullanıcılar silinemedi:\n" + DbManager::instance().lastError());
    }
}
