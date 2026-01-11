/**
 * @file tuya_ble_ancs_cfg.h
 * @brief ANCS-specific BLE security configuration
 *
 * This file defines security settings required for ANCS (Apple Notification Center Service).
 * ANCS requires pairing, encryption, and bonding before the service will be revealed.
 *
 * These defines override the default settings in tuya_ble_cfg.h to enable security.
 */

#ifndef TUYA_BLE_ANCS_CFG_H
#define TUYA_BLE_ANCS_CFG_H

// Enable Security Manager (required for pairing/encryption)
#ifndef TY_HS_BLE_SM
#define TY_HS_BLE_SM (1)
#endif

// Enable bonding (required for ANCS - iOS requires bonded devices)
#ifndef TY_HS_BLE_SM_BONDING
#define TY_HS_BLE_SM_BONDING (1)
#endif

// IO Capabilities: DISPLAY_ONLY (device can display passkey but not input)
// This works well with iOS devices which can handle display-only pairing
// Value 0x00 = BLE_HS_IO_DISPLAY_ONLY (from ble_hs.h)
#ifndef TY_HS_BLE_SM_IO_CAP
#define TY_HS_BLE_SM_IO_CAP (0x00)
#endif

// Enable Secure Connections (SC) for better security (iOS compatible)
// Note: Some older iOS versions may require legacy pairing, but modern iOS supports SC
#ifndef TY_HS_BLE_SM_SC
#define TY_HS_BLE_SM_SC (1)
#endif

// MITM (Man-in-the-Middle) protection - enabled for better security
#ifndef TY_HS_BLE_SM_MITM
#define TY_HS_BLE_SM_MITM (1)
#endif

// Maximum number of simultaneous security procedures
#ifndef TY_HS_BLE_SM_MAX_PROCS
#define TY_HS_BLE_SM_MAX_PROCS (1)
#endif

// Key distribution - we'll distribute keys for bonding
#ifndef TY_HS_BLE_SM_OUR_KEY_DIST
#define TY_HS_BLE_SM_OUR_KEY_DIST (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID)
#endif

#ifndef TY_HS_BLE_SM_THEIR_KEY_DIST
#define TY_HS_BLE_SM_THEIR_KEY_DIST (BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID)
#endif

// Maximum number of bonded devices to store
#ifndef TY_HS_BLE_STORE_MAX_BONDS
#define TY_HS_BLE_STORE_MAX_BONDS (5)
#endif

#endif /* TUYA_BLE_ANCS_CFG_H */

