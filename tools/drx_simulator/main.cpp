/**
 * @file main.cpp
 * @brief Entry point for the NETA DRX9000 Virtual Receiver Simulator.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QTextCodec>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include "simulator_window.h"

int main(int argc, char *argv[]) {
    // Disable Qt5 bearer network adapter polling that delays Windows startup (set to 24h to avoid negative interval warning)
    qputenv("QT_BEARER_POLL_TIMEOUT", QByteArray::number(86400000));

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/neta_sim_app.ico"));
    app.setFont(QFont("Segoe UI", 9));
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    QCommandLineParser parser;
    parser.setApplicationDescription("NETA DRX9000 Virtual Receiver Simulator");
    parser.addHelpOption();

    QCommandLineOption portOption(QStringList() << "p" << "port", "TCP listen port (default: 8080).", "port");
    QCommandLineOption nameOption(QStringList() << "n" << "name" << "title", "Simulator window title / role tag.", "name");
    QCommandLineOption autoStartOption(QStringList() << "s" << "start" << "auto-start", "Automatically start listening on launch.");

    parser.addOption(portOption);
    parser.addOption(nameOption);
    parser.addOption(autoStartOption);
    parser.process(app);

    SimulatorWindow win;
    if (parser.isSet(portOption)) {
        win.setListenPort(parser.value(portOption).toUShort());
    }
    if (parser.isSet(nameOption)) {
        win.setCustomTitle(parser.value(nameOption));
    }
    if (parser.isSet(autoStartOption)) {
        win.startListening();
    }

    win.show();
    return app.exec();
}
