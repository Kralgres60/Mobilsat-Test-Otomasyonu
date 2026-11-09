#include "style_helper.h"

QString StyleHelper::getAppStyle() {
    return R"(
        QWidget {
            background-color: #121418;
            color: #E0E6ED;
            font-family: 'Segoe UI', Arial, sans-serif;
            font-size: 13px;
        }

        QGroupBox {
            border: 1px solid #2B3240;
            border-radius: 8px;
            margin-top: 18px;
            font-weight: bold;
            color: #64B5F6;
            padding: 12px;
            background-color: #1A1F26;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 14px;
            padding: 0 5px;
        }

        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            background-color: #242B35;
            border: 1px solid #3A4659;
            border-radius: 6px;
            padding: 6px 10px;
            color: #FFFFFF;
            font-size: 13px;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border: 1px solid #2196F3;
            background-color: #2A3340;
        }

        QPushButton {
            background-color: #1976D2;
            color: #FFFFFF;
            font-weight: bold;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #2196F3;
        }
        QPushButton:pressed {
            background-color: #0D47A1;
        }
        QPushButton:disabled {
            background-color: #374151;
            color: #9CA3AF;
        }

        QTableWidget {
            background-color: #1A1F26;
            border: 1px solid #2B3240;
            gridline-color: #2B3240;
            border-radius: 6px;
            selection-background-color: #1976D2;
            color: #E0E6ED;
        }
        QHeaderView::section {
            background-color: #242B35;
            color: #90CAF9;
            padding: 6px;
            font-weight: bold;
            border: 1px solid #2B3240;
        }

        QTabWidget::pane {
            border: 1px solid #2B3240;
            border-radius: 6px;
            background-color: #1A1F26;
        }
        QTabBar::tab {
            background-color: #242B35;
            color: #9CA3AF;
            padding: 8px 20px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
            font-weight: bold;
        }
        QTabBar::tab:selected {
            background-color: #1976D2;
            color: #FFFFFF;
        }

        QScrollBar:vertical {
            background: #1A1F26;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: #3A4659;
            border-radius: 5px;
        }
    )";
}

QString StyleHelper::getPassBadgeStyle() {
    return "background-color: #2E7D32; color: #FFFFFF; font-weight: bold; border-radius: 6px; padding: 4px 8px;";
}

QString StyleHelper::getFailBadgeStyle() {
    return "background-color: #C62828; color: #FFFFFF; font-weight: bold; border-radius: 6px; padding: 4px 8px;";
}

QString StyleHelper::getWarningBadgeStyle() {
    return "background-color: #EF6C00; color: #FFFFFF; font-weight: bold; border-radius: 6px; padding: 4px 8px;";
}

QString StyleHelper::getPendingBadgeStyle() {
    return "background-color: #374151; color: #9CA3AF; font-weight: bold; border-radius: 6px; padding: 4px 8px;";
}

QString StyleHelper::getActiveBadgeStyle() {
    return "background-color: #1565C0; color: #FFFFFF; font-weight: bold; border-radius: 6px; padding: 4px 8px;";
}
