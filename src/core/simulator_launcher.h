/**
 * @file simulator_launcher.h
 * @brief Shared helper for locating and launching the companion NETA_DRX_Simulator executable.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef SIMULATOR_LAUNCHER_H
#define SIMULATOR_LAUNCHER_H

#include <QWidget>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QMessageBox>
#include "core/app_logger.h"

/**
 * @class SimulatorLauncher
 * @brief Locates `NETA_DRX_Simulator.exe` in installed (`tools/drx_simulator/`) or
 *        development (`tools/drx_simulator/OUTPUT/`) directories and launches it detached.
 */
class SimulatorLauncher {
public:
    /**
     * @brief Searches known deployment and build output paths for `NETA_DRX_Simulator.exe`
     *        and starts it as an independent process.
     * @param parent Optional parent widget for error dialogs.
     * @return True if the simulator executable was found and started successfully.
     */
    static bool launch(QWidget *parent = nullptr) {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            appDir + "/tools/drx_simulator/NETA_DRX_Simulator.exe",
            appDir + "/NETA_DRX_Simulator.exe",
            QDir::currentPath() + "/tools/drx_simulator/OUTPUT/NETA_DRX_Simulator.exe",
            appDir + "/../../tools/drx_simulator/OUTPUT/NETA_DRX_Simulator.exe",
            appDir + "/../../../tools/drx_simulator/OUTPUT/NETA_DRX_Simulator.exe",
            QStringLiteral("D:/A_SCIENCES/PROJECTS/NETA/DRX-Antenna-Test/tools/drx_simulator/OUTPUT/NETA_DRX_Simulator.exe")
        };

        QString foundExe;
        for (const QString &p : candidates) {
            QFileInfo fi(p);
            if (fi.exists() && fi.isFile()) {
                foundExe = fi.absoluteFilePath();
                break;
            }
        }

        if (foundExe.isEmpty()) {
            AppLogger::instance().logEvent("SIMULATOR", "NETA_DRX_Simulator.exe bulunamadi.", true);
            QMessageBox::warning(
                parent,
                QStringLiteral("Simülatör Bulunamadı"),
                QStringLiteral("NETA_DRX_Simulator.exe bulunamadı.\n\n"
                               "Lütfen tools/drx_simulator/build_sim.bat veya build.bat ile simülatörü derleyiniz."));
            return false;
        }

        const QString workDir = QFileInfo(foundExe).absolutePath();
        if (!QProcess::startDetached(foundExe, {}, workDir)) {
            AppLogger::instance().logEvent("SIMULATOR", "Simülatör başlatılamadı: " + foundExe, true);
            QMessageBox::warning(parent, QStringLiteral("Başlatma Hatası"),
                                 QStringLiteral("Simülatör başlatılamadı:\n%1").arg(foundExe));
            return false;
        }

        AppLogger::instance().logEvent("SIMULATOR", "Simülatör başlatıldı: " + foundExe);
        return true;
    }
};

#endif // SIMULATOR_LAUNCHER_H
