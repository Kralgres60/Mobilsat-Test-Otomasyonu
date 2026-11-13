/**
 * @file style_helper.cpp
 * @brief Implementation of the NETA Green QPalette and QSS stylesheets.
 *
 * NETA Green Color Palette:
 *   #071210  – deepest background
 *   #0A1F17  – surface / sidebar
 *   #112D21  – panel / groupbox
 *   #1A3D2B  – border
 *   #10B981  – accent (Emerald 500)
 *   #34D399  – accent2 / highlight (Emerald 400)
 *   #E6F4EE  – primary text
 *   #A7C4B5  – secondary text / muted
 *   #064E3B  – header bar
 *
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "style_helper.h"
#include <QApplication>
#include <QPalette>
#include <QColor>

/**
 * @brief Applies the NETA Green QPalette across the Qt application instance.
 * @param app Pointer to the QApplication instance.
 */
void StyleHelper::applyAppPalette(QApplication *app) {
    if (!app) return;

    QPalette palette;
    palette.setColor(QPalette::Window,          QColor("#071210"));
    palette.setColor(QPalette::WindowText,       QColor("#E6F4EE"));
    palette.setColor(QPalette::Base,             QColor("#0A1F17"));
    palette.setColor(QPalette::AlternateBase,    QColor("#0D2318"));
    palette.setColor(QPalette::ToolTipBase,      QColor("#112D21"));
    palette.setColor(QPalette::ToolTipText,      QColor("#34D399"));
    palette.setColor(QPalette::Text,             QColor("#E6F4EE"));
    palette.setColor(QPalette::Button,           QColor("#112D21"));
    palette.setColor(QPalette::ButtonText,       QColor("#E6F4EE"));
    palette.setColor(QPalette::Highlight,        QColor("#10B981"));
    palette.setColor(QPalette::HighlightedText,  QColor("#071210"));
    app->setPalette(palette);
}

/**
 * @brief Returns the global NETA Green application QSS stylesheet.
 * @return QString containing the full application stylesheet.
 */
QString StyleHelper::getAppStyle() {
    return R"(
        QMainWindow, QDialog, QWidget {
            background-color: #071210;
            color: #E6F4EE;
            font-family: 'Segoe UI', Arial, sans-serif;
        }

        QMenuBar {
            background-color: #064E3B;
            color: #E6F4EE;
            border-bottom: 1px solid #1A3D2B;
            font-weight: bold;
            padding: 2px 4px;
        }
        QMenuBar::item {
            background-color: transparent;
            color: #E6F4EE;
            padding: 6px 12px;
            font-weight: bold;
        }
        QMenuBar::item:selected {
            background-color: #0A1F17;
            color: #34D399;
            border-radius: 4px;
        }

        QMenu {
            background-color: #0A1F17;
            color: #E6F4EE;
            border: 1px solid #1A3D2B;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 16px;
            min-width: 170px;
            border-radius: 4px;
            color: #E6F4EE;
        }
        QMenu::item:selected {
            background-color: #10B981;
            color: #071210;
            font-weight: bold;
        }
        QMenu::separator {
            height: 1px;
            background: #1A3D2B;
            margin: 4px 8px;
        }

        QLabel {
            color: #A7C4B5;
            font-size: 9.5pt;
            background: transparent;
        }

        QGroupBox {
            font-weight: bold;
            font-size: 10pt;
            color: #34D399;
            border: 1px solid #1A3D2B;
            border-radius: 6px;
            margin-top: 10px;
            padding: 14px 10px 10px 10px;
            background-color: #0A1F17;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 6px;
            color: #34D399;
            background: transparent;
        }

        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: #0A1F17;
            color: #E6F4EE;
            border: 1px solid #1A3D2B;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 9.5pt;
            min-height: 26px;
            selection-background-color: #10B981;
            selection-color: #071210;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border: 1px solid #10B981;
        }
        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
            background-color: #071A14;
            color: #4B7A66;
            border-color: #1A3D2B;
        }
        QComboBox::drop-down {
            border: none;
            width: 22px;
        }
        QComboBox QAbstractItemView {
            background-color: #0A1F17;
            color: #E6F4EE;
            selection-background-color: #10B981;
            selection-color: #071210;
            border: 1px solid #1A3D2B;
            padding: 4px;
        }

        QCheckBox, QRadioButton {
            color: #E6F4EE;
            font-size: 9.5pt;
            spacing: 6px;
            background: transparent;
        }

        QPushButton {
            background-color: #112D21;
            color: #E6F4EE;
            border: 1px solid #1A3D2B;
            border-radius: 4px;
            padding: 6px 14px;
            font-weight: bold;
            font-size: 9.5pt;
        }
        QPushButton:hover {
            background-color: #1A3D2B;
            color: #34D399;
            border-color: #10B981;
        }
        QPushButton:pressed {
            background-color: #10B981;
            color: #071210;
        }
        QPushButton:disabled {
            background-color: #091912;
            color: #3D6652;
            border-color: #1A3D2B;
        }

        /* Sidebar Navigation (QListWidget) */
        QListWidget#sideNav {
            background-color: #0A1F17;
            border: none;
            border-right: 2px solid #1A3D2B;
            color: #A7C4B5;
            font-size: 10pt;
            font-weight: bold;
            outline: none;
        }
        QListWidget#sideNav::item {
            padding: 12px 16px;
            border-left: 3px solid transparent;
            color: #A7C4B5;
        }
        QListWidget#sideNav::item:hover {
            background-color: #112D21;
            color: #E6F4EE;
            border-left: 3px solid #34D399;
        }
        QListWidget#sideNav::item:selected {
            background-color: #164832;
            color: #34D399;
            border-left: 3px solid #10B981;
        }

        QTabWidget::pane {
            border: 1px solid #1A3D2B;
            background-color: #071210;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #0A1F17;
            color: #4B7A66;
            padding: 6px 14px;
            font-size: 9pt;
            font-weight: bold;
            border-top-left-radius: 5px;
            border-top-right-radius: 5px;
            margin-right: 2px;
            min-height: 24px;
        }
        QTabBar::tab:hover {
            color: #A7C4B5;
            background-color: #112D21;
        }
        QTabBar::tab:selected {
            background-color: #071210;
            color: #34D399;
            border-bottom: 2px solid #10B981;
        }

        QTableWidget, QTableView, QTextEdit, QPlainTextEdit, QListWidget, QTreeWidget {
            background-color: #0A1F17;
            color: #E6F4EE;
            gridline-color: #1A3D2B;
            border: 1px solid #1A3D2B;
            border-radius: 6px;
            selection-background-color: #1A3D2B;
            selection-color: #34D399;
        }
        QTableWidget::item:alternate, QTableView::item:alternate {
            background-color: #0C2219;
            color: #E6F4EE;
        }
        QHeaderView::section {
            background-color: #071210;
            color: #34D399;
            font-weight: bold;
            padding: 6px;
            border: 1px solid #1A3D2B;
        }

        QScrollBar:vertical, QScrollBar:horizontal {
            background-color: #071210;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background-color: #1A3D2B;
            border-radius: 4px;
            min-height: 20px;
        }
        QScrollBar::handle:hover {
            background-color: #10B981;
        }

        QStatusBar {
            background-color: #071210;
            color: #4B7A66;
            border-top: 1px solid #1A3D2B;
            font-size: 9pt;
        }

        QProgressBar {
            border: 1px solid #1A3D2B;
            border-radius: 4px;
            text-align: center;
            background-color: #0A1F17;
            color: #E6F4EE;
            font-weight: bold;
        }
        QProgressBar::chunk {
            background-color: #10B981;
            border-radius: 3px;
        }

        QSplitter::handle {
            background-color: #1A3D2B;
            width: 2px;
        }
        QSplitter::handle:hover {
            background-color: #10B981;
        }

        QFrame[frameShape="4"],
        QFrame[frameShape="5"] {
            color: #1A3D2B;
        }
    )";
}

/**
 * @brief Returns green badge style for PASS / Ready states.
 * @return Inline QSS string.
 */
QString StyleHelper::getPassBadgeStyle() {
    return "background-color: #D1FAE5; color: #065F46; border: 1px solid #6EE7B7; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

/**
 * @brief Returns red badge style for FAIL / Disconnected states.
 * @return Inline QSS string.
 */
QString StyleHelper::getFailBadgeStyle() {
    return "background-color: #FEE2E2; color: #B91C1C; border: 1px solid #FCA5A5; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

/**
 * @brief Returns amber badge style for Warning / Queue Waiting states.
 * @return Inline QSS string.
 */
QString StyleHelper::getWarningBadgeStyle() {
    return "background-color: #FEF3C7; color: #92400E; border: 1px solid #FCD34D; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

/**
 * @brief Returns neutral slate badge style for Pending / Idle states.
 * @return Inline QSS string.
 */
QString StyleHelper::getPendingBadgeStyle() {
    return "background-color: #1A3D2B; color: #A7C4B5; border: 1px solid #2D5E45; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

/**
 * @brief Returns accent green badge style for Active / Running states.
 * @return Inline QSS string.
 */
QString StyleHelper::getActiveBadgeStyle() {
    return "background-color: #ECFDF5; color: #064E3B; border: 1px solid #6EE7B7; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel blue telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelBlueStyle() {
    return "background-color: #DBEAFE; color: #1E40AF; border: 1px solid #93C5FD; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel green telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelGreenStyle() {
    return "background-color: #D1FAE5; color: #065F46; border: 1px solid #6EE7B7; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel amber telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelAmberStyle() {
    return "background-color: #FEF3C7; color: #92400E; border: 1px solid #FCD34D; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel orange telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelOrangeStyle() {
    return "background-color: #FFEDD5; color: #9A3412; border: 1px solid #FDBA74; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel red telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelRedStyle() {
    return "background-color: #FEE2E2; color: #B91C1C; border: 1px solid #FCA5A5; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel purple telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelPurpleStyle() {
    return "background-color: #F3E8FF; color: #6B21A8; border: 1px solid #D8B4FE; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel cyan telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelCyanStyle() {
    return "background-color: #E0F2FE; color: #0369A1; border: 1px solid #7DD3FC; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

/**
 * @brief Returns high-contrast pastel slate telemetry pill stylesheet.
 * @return Inline QSS string.
 */
QString StyleHelper::getPastelSlateStyle() {
    return "background-color: #F1F5F9; color: #1E293B; border: 1px solid #CBD5E1; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}
