/**
 * @file about_dialog.h
 * @brief Corporate and developer credits dialog.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef ABOUT_DIALOG_H
#define ABOUT_DIALOG_H

#include <QDialog>

/**
 * @class AboutDialog
 * @brief Displays system version, corporate trademarks, and primary developer attribution.
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override = default;

private:
    void setupUi();
};

#endif // ABOUT_DIALOG_H
