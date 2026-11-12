/**
 * @file user_manager_widget.h
 * @brief Host-side User & Role Authorization manager widget backed by the central MariaDB users table.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef USER_MANAGER_WIDGET_H
#define USER_MANAGER_WIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVector>
#include "core/data_models.h"

/**
 * @class UserManagerWidget
 * @brief Administrator panel for creating users, assigning Engineer (ADMIN) or Technician (OPERATOR)
 *        roles, toggling active status, and resetting passwords.
 */
class UserManagerWidget : public QWidget {
    Q_OBJECT
public:
    /** @brief Constructs the user authorization management widget. */
    explicit UserManagerWidget(QWidget *parent = nullptr);

    /** @brief Reloads all user accounts from the database into the table. */
    void reloadUsers();

signals:
    /** @brief Emitted when user records are created, updated, or deleted. */
    void logMessage(const QString &msg);
    /** @brief Emitted after user roles or active flags are saved so Host can broadcast live updates. */
    void usersUpdated();

private slots:
    void onAddUserClicked();
    void onSaveAllClicked();
    void onChangePasswordClicked();
    void onDeleteUserClicked();

private:
    void setupUi();

    QTableWidget *m_tableUsers = nullptr;
    QLabel       *m_lblStatus  = nullptr;
    QVector<UserRecord> m_users;
};

#endif // USER_MANAGER_WIDGET_H
