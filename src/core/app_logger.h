/**
 * @file app_logger.h
 * @brief Thread-safe persistent logger for both production process transactions and application events.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QFile>

/**
 * @class AppLogger
 * @brief Singleton logger that captures all Qt diagnostic messages (`qInfo`, `qWarning`, `qCritical`)
 *        and explicit process/event records into daily rotating files under `logs/`.
 */
class AppLogger : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Returns the global AppLogger singleton instance.
     * @return Reference to AppLogger.
     */
    static AppLogger& instance();

    /**
     * @brief Initializes log directory, opens daily event & process log files,
     *        and installs the Qt global message handler.
     * @param customDir Optional explicit logs directory path; defaults to `<appDir>/logs`.
     */
    void init(const QString &customDir = QString());

    /**
     * @brief Writes an application event entry (network, DB, login, UI, hardware) to `neta_events_*.log`.
     * @param category Short subsystem tag (e.g., "HOST", "DB", "DRX", "TCP").
     * @param message Human-readable event description.
     * @param isError True if this event represents an error or warning condition.
     */
    void logEvent(const QString &category, const QString &message, bool isError = false);

    /**
     * @brief Writes a production/test process transaction record to `neta_process_*.log`.
     * @param actor Station ID or role label (e.g., "HOST", "ST-1").
     * @param action Process step identifier (e.g., "TEST_START", "CHANNEL_PASS", "TEST_END").
     * @param details Detailed parameters or measurement summary.
     */
    void logProcess(const QString &actor, const QString &action, const QString &details);

    /**
     * @brief Returns the active directory where log files are persisted.
     * @return Absolute path to the logs directory.
     */
    QString logsDirectory() const;

    /**
     * @brief Global Qt message handler callback forwarding `qDebug`/`qInfo`/`qWarning`/`qCritical` to disk.
     * @param type Qt message severity level.
     * @param context Source file/line metadata.
     * @param msg Formatted message string.
     */
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);

signals:
    /**
     * @brief Emitted whenever a formatted event line is written.
     * @param formattedLine Timestamped event log line.
     */
    void eventLogged(const QString &formattedLine);

    /**
     * @brief Emitted whenever a formatted process line is written.
     * @param formattedLine Timestamped process log line.
     */
    void processLogged(const QString &formattedLine);

private:
    explicit AppLogger(QObject *parent = nullptr);
    ~AppLogger() override;

    void writeToFile(QFile &file, const QString &suffixName, const QString &line);

    mutable QMutex m_mutex;
    QString m_logDir;
    QString m_currentDateTag;
    QFile m_eventFile;
    QFile m_processFile;
    bool m_initialized = false;
};

#endif // APP_LOGGER_H
