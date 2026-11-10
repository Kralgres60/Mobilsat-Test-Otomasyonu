/**
 * @file style_helper.h
 * @brief NETA Blue corporate theme QSS stylesheets, palette setup, and high-contrast telemetry badge helpers.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef STYLE_HELPER_H
#define STYLE_HELPER_H

#include <QString>

class QApplication;

/**
 * @class StyleHelper
 * @brief Provides the unified NETA Blue theme (inspired by DRX Test Interface),
 *        application QPalette setup, and high-contrast pastel telemetry pill stylesheets.
 */
class StyleHelper {
public:
    /** @brief Applies the NETA Blue QPalette colors across the Qt application instance. */
    static void applyAppPalette(QApplication *app);

    /** @brief Returns the global NETA Blue application QSS stylesheet. */
    static QString getAppStyle();

    // Verdict and connection status badges
    /** @brief Returns green badge style for PASS / Ready states. */
    static QString getPassBadgeStyle();
    /** @brief Returns red badge style for FAIL / Disconnected states. */
    static QString getFailBadgeStyle();
    /** @brief Returns amber badge style for Warning / Queue Waiting states. */
    static QString getWarningBadgeStyle();
    /** @brief Returns neutral slate badge style for Pending / Idle states. */
    static QString getPendingBadgeStyle();
    /** @brief Returns accent blue badge style for Active / Running states. */
    static QString getActiveBadgeStyle();

    // High-contrast pastel pill helpers (matching DRX Test Interface telemetry cards)
    /** @brief Returns high-contrast pastel blue telemetry pill stylesheet. */
    static QString getPastelBlueStyle();
    /** @brief Returns high-contrast pastel green telemetry pill stylesheet. */
    static QString getPastelGreenStyle();
    /** @brief Returns high-contrast pastel amber telemetry pill stylesheet. */
    static QString getPastelAmberStyle();
    /** @brief Returns high-contrast pastel orange telemetry pill stylesheet. */
    static QString getPastelOrangeStyle();
    /** @brief Returns high-contrast pastel red telemetry pill stylesheet. */
    static QString getPastelRedStyle();
    /** @brief Returns high-contrast pastel purple telemetry pill stylesheet. */
    static QString getPastelPurpleStyle();
    /** @brief Returns high-contrast pastel cyan telemetry pill stylesheet. */
    static QString getPastelCyanStyle();
    /** @brief Returns high-contrast pastel slate telemetry pill stylesheet. */
    static QString getPastelSlateStyle();
};

#endif // STYLE_HELPER_H
