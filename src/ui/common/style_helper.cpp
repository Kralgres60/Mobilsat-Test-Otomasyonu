/**
 * @file style_helper.cpp
 * @brief Implementation of the NETA Blue QPalette and QSS stylesheets.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#include "style_helper.h"
#include <QApplication>
#include <QPalette>
#include <QColor>

void StyleHelper::applyAppPalette(QApplication *app) {
    if (!app) return;

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#0A192F"));
    palette.setColor(QPalette::WindowText, QColor("#E6F1FF"));
    palette.setColor(QPalette::Base, QColor("#112240"));
    palette.setColor(QPalette::AlternateBase, QColor("#0F1D36"));
    palette.setColor(QPalette::ToolTipBase, QColor("#112240"));
    palette.setColor(QPalette::ToolTipText, QColor("#64FFDA"));
    palette.setColor(QPalette::Text, QColor("#E6F1FF"));
    palette.setColor(QPalette::Button, QColor("#112240"));
    palette.setColor(QPalette::ButtonText, QColor("#E6F1FF"));
    palette.setColor(QPalette::Highlight, QColor("#64FFDA"));
    palette.setColor(QPalette::HighlightedText, QColor("#0A192F"));
    app->setPalette(palette);
}

QString StyleHelper::getAppStyle() {
    return R"(
        QMainWindow, QDialog, QWidget {
            background-color: #0A192F;
            color: #E6F1FF;
            font-family: 'Segoe UI', Arial, sans-serif;
        }

        QMenuBar {
            background-color: #0A192F;
            color: #E6F1FF;
            border-bottom: 1px solid #1E2D4A;
            font-weight: bold;
            padding: 2px 4px;
        }
        QMenuBar::item {
            background-color: transparent;
            color: #E6F1FF;
            padding: 6px 12px;
            font-weight: bold;
        }
        QMenuBar::item:selected {
            background-color: #112240;
            color: #64FFDA;
            border-radius: 4px;
        }

        QMenu {
            background-color: #0A192F;
            color: #E6F1FF;
            border: 1px solid #1E2D4A;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 16px;
            min-width: 170px;
            border-radius: 4px;
            color: #E6F1FF;
        }
        QMenu::item:selected {
            background-color: #64FFDA;
            color: #0A192F;
            font-weight: bold;
        }
        QMenu::separator {
            height: 1px;
            background: #1E2D4A;
            margin: 4px 8px;
        }

        QLabel {
            color: #CCD6F6;
            font-size: 9.5pt;
            background: transparent;
        }

        QGroupBox {
            font-weight: bold;
            font-size: 10pt;
            color: #64FFDA;
            border: 1px solid #1E2D4A;
            border-radius: 6px;
            margin-top: 10px;
            padding: 14px 10px 10px 10px;
            background-color: #0A192F;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 6px;
            color: #64FFDA;
            background: transparent;
        }

        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: #112240;
            color: #E6F1FF;
            border: 1px solid #233554;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 9.5pt;
            min-height: 26px;
            selection-background-color: #64FFDA;
            selection-color: #0A192F;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border: 1px solid #64FFDA;
        }
        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
            background-color: #0E1B33;
            color: #64748B;
            border-color: #1E2D4A;
        }
        QComboBox::drop-down {
            border: none;
            width: 22px;
        }
        QComboBox QAbstractItemView {
            background-color: #112240;
            color: #E6F1FF;
            selection-background-color: #64FFDA;
            selection-color: #0A192F;
            border: 1px solid #233554;
            padding: 4px;
        }

        QCheckBox, QRadioButton {
            color: #E6F1FF;
            font-size: 9.5pt;
            spacing: 6px;
            background: transparent;
        }

        QPushButton {
            background-color: #112240;
            color: #E6F1FF;
            border: 1px solid #1E2D4A;
            border-radius: 4px;
            padding: 6px 14px;
            font-weight: bold;
            font-size: 9.5pt;
        }
        QPushButton:hover {
            background-color: #233554;
            color: #64FFDA;
            border-color: #64FFDA;
        }
        QPushButton:pressed {
            background-color: #1E2D4A;
        }
        QPushButton:disabled {
            background-color: #0D1B30;
            color: #475569;
            border-color: #1E2D4A;
        }

        QTabWidget::pane {
            border: 1px solid #1E2D4A;
            background-color: #0A192F;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #112240;
            color: #8892B0;
            padding: 6px 14px;
            font-size: 9pt;
            font-weight: bold;
            border-top-left-radius: 5px;
            border-top-right-radius: 5px;
            margin-right: 2px;
            min-height: 24px;
        }
        QTabBar::tab:hover {
            color: #CCD6F6;
            background-color: #1A2E52;
        }
        QTabBar::tab:selected {
            background-color: #0A192F;
            color: #64FFDA;
            border-bottom: 2px solid #64FFDA;
        }

        QTableWidget, QTableView, QTextEdit, QPlainTextEdit, QListWidget, QTreeWidget {
            background-color: #112240;
            color: #E6F1FF;
            gridline-color: #1E2D4A;
            border: 1px solid #1E2D4A;
            border-radius: 6px;
            selection-background-color: #233554;
            selection-color: #64FFDA;
        }
        QTableWidget::item:alternate, QTableView::item:alternate {
            background-color: #0F1E3A;
            color: #E6F1FF;
        }
        QHeaderView::section {
            background-color: #0A192F;
            color: #64FFDA;
            font-weight: bold;
            padding: 6px;
            border: 1px solid #1E2D4A;
        }

        QScrollBar:vertical, QScrollBar:horizontal {
            background-color: #0A192F;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background-color: #1E2D4A;
            border-radius: 4px;
            min-height: 20px;
        }
        QScrollBar::handle:hover {
            background-color: #64FFDA;
        }

        QStatusBar {
            background-color: #0A192F;
            color: #8892B0;
            border-top: 1px solid #1E2D4A;
            font-size: 9pt;
        }

        QProgressBar {
            border: 1px solid #1E2D4A;
            border-radius: 4px;
            text-align: center;
            background-color: #112240;
            color: #E6F1FF;
            font-weight: bold;
        }
        QProgressBar::chunk {
            background-color: #64FFDA;
            border-radius: 3px;
        }
    )";
}

QString StyleHelper::getPassBadgeStyle() {
    return "background-color: #D1FAE5; color: #065F46; border: 1px solid #6EE7B7; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

QString StyleHelper::getFailBadgeStyle() {
    return "background-color: #FEE2E2; color: #B91C1C; border: 1px solid #FCA5A5; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

QString StyleHelper::getWarningBadgeStyle() {
    return "background-color: #FEF3C7; color: #92400E; border: 1px solid #FCD34D; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

QString StyleHelper::getPendingBadgeStyle() {
    return "background-color: #1E293B; color: #94A3B8; border: 1px solid #334155; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

QString StyleHelper::getActiveBadgeStyle() {
    return "background-color: #DBEAFE; color: #1E40AF; border: 1px solid #93C5FD; font-weight: bold; border-radius: 4px; padding: 4px 8px; font-size: 9pt;";
}

QString StyleHelper::getPastelBlueStyle() {
    return "background-color: #DBEAFE; color: #1E40AF; border: 1px solid #93C5FD; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelGreenStyle() {
    return "background-color: #D1FAE5; color: #065F46; border: 1px solid #6EE7B7; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelAmberStyle() {
    return "background-color: #FEF3C7; color: #92400E; border: 1px solid #FCD34D; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelOrangeStyle() {
    return "background-color: #FFEDD5; color: #9A3412; border: 1px solid #FDBA74; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelRedStyle() {
    return "background-color: #FEE2E2; color: #B91C1C; border: 1px solid #FCA5A5; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelPurpleStyle() {
    return "background-color: #F3E8FF; color: #6B21A8; border: 1px solid #D8B4FE; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelCyanStyle() {
    return "background-color: #E0F2FE; color: #0369A1; border: 1px solid #7DD3FC; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}

QString StyleHelper::getPastelSlateStyle() {
    return "background-color: #F1F5F9; color: #1E293B; border: 1px solid #CBD5E1; border-radius: 4px; padding: 4px 8px; font-weight: bold; font-size: 9pt;";
}
