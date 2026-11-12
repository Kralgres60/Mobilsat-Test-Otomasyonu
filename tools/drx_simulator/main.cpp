/**
 * @file main.cpp
 * @brief Entry point for the NETA DRX9000 Virtual Receiver Simulator.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include <QApplication>
#include <QFont>
#include <QTextCodec>
#include "simulator_window.h"

int main(int argc, char *argv[]) {
    // Disable Qt5 bearer network adapter polling that delays Windows startup
    qputenv("QT_BEARER_POLL_TIMEOUT", QByteArray::number(-1));

    QApplication app(argc, argv);
    app.setFont(QFont("Segoe UI", 9));
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    SimulatorWindow win;
    win.show();
    return app.exec();
}
