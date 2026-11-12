/**
 * @file app_logger.cpp
 * @brief Implementation of persistent process and application event file logging.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "app_logger.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMutexLocker>
#include <QTextStream>
#include <cstdio>

AppLogger& AppLogger::instance() {
    static AppLogger s_instance;
    return s_instance;
}

AppLogger::AppLogger(QObject *parent) : QObject(parent) {
}

AppLogger::~AppLogger() {
    QMutexLocker lock(&m_mutex);
    if (m_eventFile.isOpen()) {
        m_eventFile.flush();
        m_eventFile.close();
    }
    if (m_processFile.isOpen()) {
        m_processFile.flush();
        m_processFile.close();
    }
}

void AppLogger::init(const QString &customDir) {
    QMutexLocker lock(&m_mutex);
    if (!customDir.trimmed().isEmpty()) {
        m_logDir = QDir::cleanPath(customDir);
    } else if (QCoreApplication::instance()) {
        m_logDir = QDir(QCoreApplication::applicationDirPath()).filePath("logs");
    } else {
        m_logDir = QDir::current().filePath("logs");
    }

    QDir().mkpath(m_logDir);
    m_currentDateTag = QDate::currentDate().toString("yyyy-MM-dd");
    m_initialized = true;

    qInstallMessageHandler(&AppLogger::qtMessageHandler);
}

QString AppLogger::logsDirectory() const {
    QMutexLocker lock(&m_mutex);
    return m_logDir;
}

void AppLogger::writeToFile(QFile &file, const QString &prefix, const QString &line) {
    if (!m_initialized) {
        return;
    }

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    if (today != m_currentDateTag || !file.isOpen()) {
        if (file.isOpen()) {
            file.close();
        }
        m_currentDateTag = today;
        const QString path = QDir(m_logDir).filePath(QString("%1_%2.log").arg(prefix, today));
        file.setFileName(path);
        file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }

    if (file.isOpen()) {
        QTextStream ts(&file);
        ts.setCodec("UTF-8");
        ts << line << "\n";
        file.flush();
    }
}

void AppLogger::logEvent(const QString &category, const QString &message, bool isError) {
    const QString line = QString("[%1] [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                                  isError ? "ERROR" : "INFO",
                                  category,
                                  message);
    {
        QMutexLocker lock(&m_mutex);
        writeToFile(m_eventFile, "neta_events", line);
    }
    emit eventLogged(line);
}

void AppLogger::logProcess(const QString &actor, const QString &action, const QString &details) {
    const QString line = QString("[%1] [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                                  actor,
                                  action,
                                  details);
    {
        QMutexLocker lock(&m_mutex);
        writeToFile(m_processFile, "neta_process", line);
    }
    emit processLogged(line);
}

void AppLogger::qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QString level = "INFO";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG"; break;
    case QtInfoMsg:     level = "INFO";  break;
    case QtWarningMsg:  level = "WARN";  break;
    case QtCriticalMsg: level = "CRIT";  break;
    case QtFatalMsg:    level = "FATAL"; break;
    }

    const QString line = QString("[%1] [QT_%2] %3")
                             .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                                  level,
                                  msg);

    AppLogger &logger = AppLogger::instance();
    {
        QMutexLocker lock(&logger.m_mutex);
        logger.writeToFile(logger.m_eventFile, "neta_events", line);
    }

    // Mirror to stderr for Qt Creator / console visibility
    fprintf(stderr, "%s\n", line.toUtf8().constData());
    fflush(stderr);
}
