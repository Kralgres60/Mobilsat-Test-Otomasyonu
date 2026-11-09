/**
 * @file style_helper.h
 * @brief Industrial dark theme QSS stylesheets and badge decoration helpers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef STYLE_HELPER_H
#define STYLE_HELPER_H

#include <QString>

/**
 * @class StyleHelper
 * @brief Provides unified dark-theme styling, palette accents, and verdict badge stylesheets.
 */
class StyleHelper {
public:
    static QString getAppStyle();
    static QString getPassBadgeStyle();
    static QString getFailBadgeStyle();
    static QString getWarningBadgeStyle();
    static QString getPendingBadgeStyle();
    static QString getActiveBadgeStyle();
};

#endif // STYLE_HELPER_H
