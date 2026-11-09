/**
 * @file mode_select_dialog.h
 * @brief Startup mode selection dialog for choosing between Station (Client) and Host operating roles.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef MODE_SELECT_DIALOG_H
#define MODE_SELECT_DIALOG_H

#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include "app_config.h"

/**
 * @class ModeSelectDialog
 * @brief Startup dialog presenting interactive cards for selecting whether this PC
 *        executes as a Test Station (Client) or as Central Controller & Reference Host.
 */
class ModeSelectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModeSelectDialog(QWidget *parent = nullptr);

    AppRole selectedRole() const { return m_selectedRole; }
    int selectedStationId() const;
    QString serverIp() const;
    bool rememberSelection() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void selectRole(AppRole role);
    void onAccept();
    void onInstallDbClicked();
    void onAboutClicked();

private:
    void setupUi();
    void updateCardStyles();
    void checkDatabaseStatus();

    AppRole m_selectedRole = AppRole::Client;

    // Role Selection Cards
    QFrame *m_cardClient = nullptr;
    QFrame *m_cardHost = nullptr;

    QLabel *m_lblClientCheck = nullptr;
    QLabel *m_lblHostCheck = nullptr;

    // Client Configuration Inputs
    QComboBox *m_comboStation = nullptr;
    QLineEdit *m_txtHostIp = nullptr;

    // Database Setup Action Button
    QPushButton *m_btnInstallDb = nullptr;
    QLabel *m_lblDbStatus = nullptr;

    // Persistence and Launch Buttons
    QCheckBox *m_chkRemember = nullptr;
    QPushButton *m_btnExit = nullptr;
    QPushButton *m_btnLaunch = nullptr;
};

#endif // MODE_SELECT_DIALOG_H
