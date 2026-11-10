/**
 * @file login_dialog.h
 * @brief Minimalist DRX-inspired authentication dialog supporting Technician and Engineer roles.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef LOGIN_DIALOG_H
#define LOGIN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include "core/app_config.h"

class QShowEvent;

/**
 * @class LoginDialog
 * @brief Clean NETA Blue authentication screen modeled after DRX Test Interface.
 *        Authenticates operators (Technicians) and supervisors (Engineers) and dispatches station/host mode.
 */
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    /** @brief Constructs the login dialog and restores the last saved username and station selection. */
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog() override = default;

    /** @brief Returns the authenticated user's privilege level (Technician or Engineer). */
    UserRole authenticatedRole() const { return m_authenticatedRole; }
    /** @brief Returns the authenticated username. */
    QString authenticatedUsername() const { return m_authenticatedUsername; }
    /** @brief Returns the authenticated user's full display name. */
    QString authenticatedFullName() const { return m_authenticatedFullName; }
    /** @brief Returns the selected application role (Client workstation or Host server). */
    AppRole selectedAppRole() const { return m_selectedAppRole; }
    /** @brief Returns the selected station number (1..3) when running in Client mode. */
    int selectedStationId() const { return m_selectedStationId; }

protected:
    /** @brief Centers the dialog cleanly on the active primary monitor on show. */
    void showEvent(QShowEvent *event) override;

private slots:
    /** @brief Validates credentials against DbManager and persists session config upon success. */
    void onLoginClicked();

    /** @brief Updates target role/station state when dropdown selection changes. */
    void onStationIndexChanged(int index);

private:
    void setupUi();

    QLineEdit *m_txtUsername = nullptr;
    QLineEdit *m_txtPassword = nullptr;
    QComboBox *m_comboRoleStation = nullptr;
    QPushButton *m_btnLogin = nullptr;
    QLabel *m_lblErrorStatus = nullptr;

    UserRole m_authenticatedRole = UserRole::Technician;
    QString m_authenticatedUsername = "operator1";
    QString m_authenticatedFullName = "Masa 1 Operatörü";
    AppRole m_selectedAppRole = AppRole::Client;
    int m_selectedStationId = 1;
};

#endif // LOGIN_DIALOG_H
