/**
 * @file login_dialog.h
 * @brief Minimalist NETA Green authentication dialog (username & password only; HOST/JOIN happens in Lobby).
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef LOGIN_DIALOG_H
#define LOGIN_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include "core/app_config.h"

class QShowEvent;

/**
 * @class LoginDialog
 * @brief Clean NETA Green authentication screen authenticating Technician and Engineer accounts
 *        before entering the unified HOST/JOIN server lobby.
 */
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    /** @brief Constructs the login dialog and restores the last saved username. */
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog() override = default;

    /** @brief Returns the authenticated user's privilege level (Technician or Engineer). */
    UserRole authenticatedRole() const { return m_authenticatedRole; }
    /** @brief Returns the authenticated username. */
    QString authenticatedUsername() const { return m_authenticatedUsername; }
    /** @brief Returns the authenticated user's full display name. */
    QString authenticatedFullName() const { return m_authenticatedFullName; }

protected:
    /** @brief Centers the dialog cleanly on the active primary monitor on show. */
    void showEvent(QShowEvent *event) override;

private slots:
    /** @brief Validates credentials against DbManager and persists session config upon success. */
    void onLoginClicked();

private:
    void setupUi();

    QLineEdit *m_txtUsername = nullptr;
    QLineEdit *m_txtPassword = nullptr;
    QPushButton *m_btnLogin = nullptr;
    QLabel *m_lblErrorStatus = nullptr;

    UserRole m_authenticatedRole = UserRole::Technician;
    QString m_authenticatedUsername = "operator1";
    QString m_authenticatedFullName = "Operatör";
};

#endif // LOGIN_DIALOG_H
