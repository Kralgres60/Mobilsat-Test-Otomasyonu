#ifndef DEPENDENCY_INSTALL_DIALOG_H
#define DEPENDENCY_INSTALL_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QFile>

class DependencyInstallDialog : public QDialog {
    Q_OBJECT
public:
    explicit DependencyInstallDialog(QWidget *parent = nullptr);
    ~DependencyInstallDialog();

    bool isSuccess() const { return m_success; }
    void startInstallation();

private slots:
    void onPollTimer();
    void onRetryClicked();
    void onCopyLogClicked();
    void onCloseOrFinishClicked();

private:
    void setupUi();
    void appendLog(const QString &line);
    void readStreamLog();
    void finishInstallation(bool success, const QString &message = QString());

    QLabel *m_lblTitle;
    QLabel *m_lblSubtitle;
    QProgressBar *m_progressBar;
    QPlainTextEdit *m_txtConsole;
    QLabel *m_lblPortStatus;
    QPushButton *m_btnCopy;
    QPushButton *m_btnRetry;
    QPushButton *m_btnClose;

    QTimer m_pollTimer;
    qint64 m_logPos = 0;
    QString m_streamLogPath;
    int m_elapsedTicks = 0;
    bool m_success = false;
    bool m_finished = false;
};

#endif // DEPENDENCY_INSTALL_DIALOG_H
