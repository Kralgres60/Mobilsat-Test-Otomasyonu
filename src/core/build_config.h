/**
 * @file build_config.h
 * @brief Central build configuration, multi-architecture switches, and debug macros.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

// ==============================================================================
// 1. SYSTEM ARCHITECTURE CONFIGURATION
// ==============================================================================
// Enable exactly one of the deployment architectures below:

// #define NETA_ALGORITMA_1_FOUR_PC     ///< 4-PC Architecture: 1 Host/Arbiter Server + 3 Stations (TCP Lock Arbiter)
#define NETA_ALGORITMA_2_THREE_PC       ///< 3-PC Architecture: 3 Peer Workstations (MariaDB Distributed Atomic Lock)

// ==============================================================================
// 2. DEVELOPER & DEBUG TEST MODES
// ==============================================================================
// Enable to bypass standard production station/host selection and directly launch
// specialized debugging consoles for hardware protocols or database testing:

#define NETA_DEBUG_MODE                 ///< Interactive DRX REST API / Web CGI and Multi-Sampling Debug Console
// #define NETA_DEBUG_DB_MODE           ///< Standalone MariaDB & Distributed Lock Verification Console

// ==============================================================================
// 3. BUILD SANITY CHECKS
// ==============================================================================
#if defined(NETA_ALGORITMA_1_FOUR_PC) && defined(NETA_ALGORITMA_2_THREE_PC)
    #error "ERROR: Both NETA_ALGORITMA_1_FOUR_PC and NETA_ALGORITMA_2_THREE_PC cannot be defined simultaneously!"
#endif

#if !defined(NETA_ALGORITMA_1_FOUR_PC) && !defined(NETA_ALGORITMA_2_THREE_PC)
    #error "ERROR: Please select either NETA_ALGORITMA_1_FOUR_PC or NETA_ALGORITMA_2_THREE_PC in build_config.h!"
#endif

#endif // BUILD_CONFIG_H
