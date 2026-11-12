/**
 * @file db_viewer_widget.h
 * @brief Administrator SQL database table browser widget for Host mode.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef DB_VIEWER_WIDGET_H
#define DB_VIEWER_WIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QStringList>

/**
 * @class DbViewerWidget
 * @brief Allows the Host Administrator to inspect live MariaDB/MySQL (or fallback SQLite)
 *        tables, row counts, and records with instant client-side filtering.
 */
class DbViewerWidget : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Constructs the database table inspector widget.
     * @param parent Optional parent widget.
     */
    explicit DbViewerWidget(QWidget *parent = nullptr);

public slots:
    /**
     * @brief Reloads the table list from the active database connection and displays the selected table.
     */
    void refreshAll();

    /**
     * @brief Queries and renders rows of the currently selected table.
     */
    void loadSelectedTable();

private slots:
    void applyFilter(const QString &text);

private:
    void setupUi();

    QComboBox    *m_comboTables = nullptr;
    QComboBox    *m_comboLimit  = nullptr;
    QLineEdit    *m_txtFilter   = nullptr;
    QPushButton  *m_btnRefresh  = nullptr;
    QLabel       *m_lblDbInfo   = nullptr;
    QTableWidget *m_table       = nullptr;
    QStringList   m_knownTables;
};

#endif // DB_VIEWER_WIDGET_H
