/**
 * @file ancs.c
 * @brief Apple Notification Center Service (ANCS) client implementation
 *
 * This module implements ANCS client functionality, allowing the board to
 * receive notifications from an iPhone. The board acts as a BLE peripheral
 * (connection role) and GATT client (to interact with ANCS service).
 *
 * Implementation Notes:
 * - This module uses nimble GATT client functions directly since we need
 *   GATT client functionality while in peripheral connection role
 * - Service discovery and characteristic subscription are handled via nimble
 * - Notifications should be received through TAL_BLE_EVT_NOTIFY_RX events
 * - If notifications don't come through TAL events, you may need to integrate
 *   with nimble's notification callback mechanism directly
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

// Include ANCS security config BEFORE other headers
// This ensures security settings are defined before tuya_ble_cfg.h is included
#include "tuya_ble_ancs_cfg.h"

// Include nimble headers FIRST to avoid LIST_HEAD macro conflicts
// (nimble defines LIST_HEAD(name, type) differently than Tuya's LIST_HEAD(name))
// We'll undefine LIST_HEAD after including nimble headers, then Tuya will redefine it
#include "ble_gatt.h"
#include "ble_uuid.h"
// Note: ble_gap.h is not included here to avoid header dependency issues
// Security/pairing will be handled via timer-based retry approach
// #include "ble_hs.h"
#include "ble_hs_mbuf.h"

// Undefine LIST_HEAD to allow Tuya's version to be defined
// (Tuya's version takes one parameter, nimble's takes two)
#ifdef LIST_HEAD
#undef LIST_HEAD
#endif

// Define BLE_HS_CONN_HANDLE_NONE instead of including ble_hs.h
// (ble_hs.h pulls in internal headers that cause conflicts)
#define BLE_HS_CONN_HANDLE_NONE     0xffff
#define BLE_HS_EDONE                (0x80)  // Error code for "done" status

// Now include Tuya headers (which will define their version of LIST_HEAD)
// Include logging headers first so PR_DEBUG is available
#include "tkl_output.h"
#include "tal_api.h"  // Provides PR_DEBUG and other logging macros

// Include ancs.h which includes tal_bluetooth.h
#include "ancs.h"

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

// Forward declarations
static int ancs_svc_disc_cb(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *service,
                           void *arg);

static int ancs_chr_disc_cb(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr,
                           void *arg);

static int ancs_desc_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            uint16_t chr_val_handle,
                            const struct ble_gatt_dsc *dsc,
                            void *arg);

static void ancs_start_service_discovery(void);
static void ancs_check_and_start_discovery(void);

// ANCS Service UUID (128-bit) - Using BLE_UUID128_INIT macro
static const ble_uuid128_t ancs_service_uuid = BLE_UUID128_INIT(
    0x79, 0x05, 0xF4, 0x31, 0xB5, 0xCE, 0x4E, 0x99,
    0xA4, 0x0F, 0x4B, 0x1E, 0x12, 0x2D, 0x00, 0xD0
);

// ANCS Notification Source Characteristic UUID (128-bit)
static const ble_uuid128_t ancs_notification_source_uuid = BLE_UUID128_INIT(
    0x9F, 0xBF, 0x12, 0x0D, 0x63, 0x01, 0x42, 0xD9,
    0x8C, 0x58, 0x25, 0xE6, 0x99, 0xA2, 0x1D, 0xBD
);

// ANCS Control Point Characteristic UUID (128-bit)
static const ble_uuid128_t ancs_control_point_uuid = BLE_UUID128_INIT(
    0x69, 0xD1, 0xD8, 0xF3, 0x45, 0xE1, 0x49, 0xA8,
    0x98, 0x21, 0x9B, 0xBD, 0xFD, 0xAA, 0xD9, 0xD9
);

// ANCS Data Source Characteristic UUID (128-bit)
static const ble_uuid128_t ancs_data_source_uuid = BLE_UUID128_INIT(
    0x22, 0xEA, 0xC6, 0xE9, 0x24, 0xD6, 0x4B, 0xB5,
    0xBE, 0x44, 0xB3, 0x6A, 0xCE, 0x7C, 0x7B, 0xFB
);

// Client Characteristic Configuration Descriptor UUID (16-bit)
static const ble_uuid16_t cccd_uuid = BLE_UUID16_INIT(0x2902);

/**
 * @brief ANCS client state
 */
typedef enum {
    ANCS_STATE_DISCONNECTED,
    ANCS_STATE_CONNECTED,
    ANCS_STATE_DISCOVERING_SERVICE,
    ANCS_STATE_DISCOVERING_CHARACTERISTICS,
    ANCS_STATE_DISCOVERING_DESCRIPTORS,
    ANCS_STATE_SUBSCRIBING,
    ANCS_STATE_READY,
} ancs_state_t;

/**
 * @brief ANCS client context
 */
static struct {
    ancs_state_t state;
    uint16_t conn_handle;
    
    // Service handles
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    
    // Characteristic handles
    uint16_t notification_source_handle;
    uint16_t control_point_handle;
    uint16_t data_source_handle;
    
    // CCCD handles
    uint16_t notification_source_cccd_handle;
    uint16_t data_source_cccd_handle;
    
    // Callbacks
    ancs_notification_callback_t notification_cb;
    ancs_notification_data_callback_t data_cb;
    
    bool service_found;
    uint8_t chars_found;
    uint8_t descs_found;
    
    // Prerequisites for service discovery
    bool mtu_exchange_complete;
    bool encryption_ready;  // Assumed ready after connection (iOS initiates pairing)
} ancs_ctx = {
    .state = ANCS_STATE_DISCONNECTED,
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
    .mtu_exchange_complete = false,
    .encryption_ready = false,
    .service_start_handle = 0,
    .service_end_handle = 0,
    .notification_source_handle = 0,
    .control_point_handle = 0,
    .data_source_handle = 0,
    .notification_source_cccd_handle = 0,
    .data_source_cccd_handle = 0,
    .notification_cb = NULL,
    .data_cb = NULL,
    .service_found = false,
    .chars_found = 0,
    .descs_found = 0,
};

/**
 * @brief Compare two UUIDs
 */
static bool ancs_uuid_equal(const ble_uuid_t *uuid1, const ble_uuid_t *uuid2)
{
    if (uuid1->type != uuid2->type) {
        return false;
    }
    
    if (uuid1->type == BLE_UUID_TYPE_128) {
        const ble_uuid128_t *u1 = (const ble_uuid128_t *)uuid1;
        const ble_uuid128_t *u2 = (const ble_uuid128_t *)uuid2;
        return memcmp(u1->value, u2->value, 16) == 0;
    }
    
    return false;
}

/**
 * @brief Service discovery callback
 */
static int ancs_svc_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_svc *service,
                            void *arg)
{
    (void)conn_handle;
    (void)arg;
    
    if (error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            // Discovery complete
            if (!ancs_ctx.service_found) {
                PR_DEBUG("ANCS: ANCS service not found (encryption may not be ready yet)");
                // Return to CONNECTED state - if encryption completes later, discovery can be retried
                ancs_ctx.state = ANCS_STATE_CONNECTED;
            } else {
                // Service found - start characteristic discovery
                PR_DEBUG("ANCS: Starting characteristic discovery");
                ancs_ctx.state = ANCS_STATE_DISCOVERING_CHARACTERISTICS;
                int rc = ble_gattc_disc_all_chrs(conn_handle,
                                                  ancs_ctx.service_start_handle,
                                                  ancs_ctx.service_end_handle,
                                                  ancs_chr_disc_cb,
                                                  NULL);
                if (rc != 0) {
                    PR_DEBUG("ANCS: Failed to start characteristic discovery, rc=%d", rc);
                    ancs_ctx.state = ANCS_STATE_CONNECTED;
                }
            }
        } else {
            PR_DEBUG("ANCS: Service discovery error, status=%d", error->status);
            // Return to CONNECTED state on error
            ancs_ctx.state = ANCS_STATE_CONNECTED;
        }
        return 0;
    }
    
    // Check if this is the ANCS service
    if (ancs_uuid_equal(&service->uuid.u, &ancs_service_uuid.u)) {
        PR_DEBUG("ANCS: Service found, start_handle=0x%04x, end_handle=0x%04x",
                 service->start_handle, service->end_handle);
        ancs_ctx.service_start_handle = service->start_handle;
        ancs_ctx.service_end_handle = service->end_handle;
        ancs_ctx.service_found = true;
    }
    
    return 0;
}

/**
 * @brief Characteristic discovery callback
 */
static int ancs_chr_disc_cb(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr,
                           void *arg)
{
    (void)conn_handle;
    (void)arg;
    
    if (error->status != 0) {
        PR_DEBUG("ANCS: Characteristic discovery failed, status=%d", error->status);
        if (error->status == BLE_HS_EDONE) {
            // Discovery complete
            PR_DEBUG("ANCS: Found %d characteristics", ancs_ctx.chars_found);
            
            // Check if we found all required characteristics
            if (ancs_ctx.notification_source_handle != 0 &&
                ancs_ctx.control_point_handle != 0 &&
                ancs_ctx.data_source_handle != 0) {
                // Start descriptor discovery for Notification Source
                PR_DEBUG("ANCS: Starting descriptor discovery");
                ancs_ctx.state = ANCS_STATE_DISCOVERING_DESCRIPTORS;
                int rc = ble_gattc_disc_all_dscs(conn_handle,
                                                  ancs_ctx.notification_source_handle,
                                                  ancs_ctx.service_end_handle,
                                                  ancs_desc_disc_cb,
                                                  NULL);
                if (rc != 0) {
                    PR_DEBUG("ANCS: Failed to start descriptor discovery, rc=%d", rc);
                    ancs_ctx.state = ANCS_STATE_CONNECTED;
                }
            } else {
                PR_DEBUG("ANCS: Missing required characteristics");
                ancs_ctx.state = ANCS_STATE_CONNECTED;
            }
        }
        return 0;
    }
    
    // Check which characteristic this is
    if (ancs_uuid_equal(&chr->uuid.u, &ancs_notification_source_uuid.u)) {
        PR_DEBUG("ANCS: Notification Source found, handle=0x%04x", chr->val_handle);
        ancs_ctx.notification_source_handle = chr->val_handle;
        ancs_ctx.chars_found++;
    } else if (ancs_uuid_equal(&chr->uuid.u, &ancs_control_point_uuid.u)) {
        PR_DEBUG("ANCS: Control Point found, handle=0x%04x", chr->val_handle);
        ancs_ctx.control_point_handle = chr->val_handle;
        ancs_ctx.chars_found++;
    } else if (ancs_uuid_equal(&chr->uuid.u, &ancs_data_source_uuid.u)) {
        PR_DEBUG("ANCS: Data Source found, handle=0x%04x", chr->val_handle);
        ancs_ctx.data_source_handle = chr->val_handle;
        ancs_ctx.chars_found++;
    }
    
    return 0;
}

/**
 * @brief Descriptor discovery callback
 */
static int ancs_desc_disc_cb(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            uint16_t chr_val_handle,
                            const struct ble_gatt_dsc *dsc,
                            void *arg)
{
    (void)conn_handle;
    (void)arg;
    
    if (error->status != 0) {
        PR_DEBUG("ANCS: Descriptor discovery failed, status=%d", error->status);
        if (error->status == BLE_HS_EDONE) {
            // Discovery complete
            PR_DEBUG("ANCS: Found %d descriptors", ancs_ctx.descs_found);
            
            // If we haven't found Data Source CCCD yet, discover it
            if (ancs_ctx.data_source_cccd_handle == 0 && ancs_ctx.data_source_handle != 0) {
                PR_DEBUG("ANCS: Discovering Data Source descriptors");
                int rc = ble_gattc_disc_all_dscs(conn_handle,
                                                  ancs_ctx.data_source_handle,
                                                  ancs_ctx.service_end_handle,
                                                  ancs_desc_disc_cb,
                                                  NULL);
                if (rc != 0) {
                    PR_DEBUG("ANCS: Failed to discover Data Source descriptors, rc=%d", rc);
                    ancs_ctx.state = ANCS_STATE_CONNECTED;
                }
                return 0;
            }
            
            // Subscribe to notifications if we have CCCD handles
            if (ancs_ctx.notification_source_cccd_handle != 0 &&
                ancs_ctx.data_source_cccd_handle != 0) {
                ancs_ctx.state = ANCS_STATE_SUBSCRIBING;
                
                // Enable notifications on Notification Source
                uint8_t cccd_value[2] = {0x01, 0x00}; // Enable notifications
                int rc = ble_gattc_write_no_rsp_flat(conn_handle,
                                                      ancs_ctx.notification_source_cccd_handle,
                                                      cccd_value,
                                                      sizeof(cccd_value));
                if (rc != 0) {
                    PR_DEBUG("ANCS: Failed to subscribe to Notification Source, rc=%d", rc);
                    ancs_ctx.state = ANCS_STATE_CONNECTED;
                    return 0;
                }
                
                // Enable notifications on Data Source
                rc = ble_gattc_write_no_rsp_flat(conn_handle,
                                                  ancs_ctx.data_source_cccd_handle,
                                                  cccd_value,
                                                  sizeof(cccd_value));
                if (rc != 0) {
                    PR_DEBUG("ANCS: Failed to subscribe to Data Source, rc=%d", rc);
                    ancs_ctx.state = ANCS_STATE_CONNECTED;
                    return 0;
                }
                
                PR_DEBUG("ANCS: Successfully subscribed to ANCS characteristics");
                ancs_ctx.state = ANCS_STATE_READY;
            } else {
                PR_DEBUG("ANCS: Missing CCCD handles");
                ancs_ctx.state = ANCS_STATE_CONNECTED;
            }
        }
        return 0;
    }
    
    // Check if this is a CCCD
    if (dsc->uuid.u.type == BLE_UUID_TYPE_16) {
        const ble_uuid16_t *uuid16 = (const ble_uuid16_t *)&dsc->uuid;
        if (uuid16->value == cccd_uuid.value) {
            PR_DEBUG("ANCS: CCCD found, handle=0x%04x, chr_val_handle=0x%04x",
                     dsc->handle, chr_val_handle);
            
            if (chr_val_handle == ancs_ctx.notification_source_handle) {
                ancs_ctx.notification_source_cccd_handle = dsc->handle;
                ancs_ctx.descs_found++;
            } else if (chr_val_handle == ancs_ctx.data_source_handle) {
                ancs_ctx.data_source_cccd_handle = dsc->handle;
                ancs_ctx.descs_found++;
            }
        }
    }
    
    return 0;
}

/**
 * @brief Parse notification source data
 */
static void ancs_parse_notification_source(const uint8_t *data, uint16_t len)
{
    if (len < 8) {
        return;
    }
    
    uint8_t event_id = data[0];
    uint8_t event_flags = data[1];
    uint8_t category_id = data[2];
    // uint8_t category_count = data[3];  // Not used currently
    uint32_t notification_uid = data[4] | (data[5] << 8) |
                                (data[6] << 16) | (data[7] << 24);
    
    PR_DEBUG("ANCS: Notification event: id=%d, flags=0x%02x, category=%d, uid=0x%08x",
             event_id, event_flags, category_id, notification_uid);
    
    if (ancs_ctx.notification_cb) {
        ancs_ctx.notification_cb(event_id, category_id, notification_uid, event_flags);
    }
}

/**
 * @brief Parse data source attribute data
 */
static void ancs_parse_data_source(const uint8_t *data, uint16_t len)
{
    if (!ancs_ctx.data_cb || len < 8) {
        return;
    }
    
    // ANCS Data Source format:
    // Command ID (1 byte) - should be 0 (Get Notification Attributes response)
    // Notification UID (4 bytes, little-endian)
    // Attribute ID (1 byte)
    // Attribute Length (2 bytes, little-endian)
    // Attribute Data (variable length)
    
    uint8_t command_id = data[0];
    uint32_t notification_uid = data[1] | (data[2] << 8) |
                                (data[3] << 16) | (data[4] << 24);
    uint8_t attribute_id = data[5];
    uint16_t attribute_len = data[6] | (data[7] << 8);
    
    if (command_id != 0 || attribute_len == 0 || len < (8 + attribute_len)) {
        return;
    }
    
    PR_DEBUG("ANCS: Data source: uid=0x%08x, attr_id=%d, len=%d",
             notification_uid, attribute_id, attribute_len);
    
    if (len >= (8 + attribute_len)) {
        ancs_ctx.data_cb(notification_uid, attribute_id, data + 8, attribute_len);
    }
}

/**
 * @brief Check prerequisites and start service discovery if ready
 * 
 * State-based approach: Only start discovery when all prerequisites are met.
 * ANCS requires:
 * - Connection established
 * - MTU exchange complete
 * - Encryption/pairing complete (assumed ready after connection with security config)
 */
static void ancs_check_and_start_discovery(void)
{
    // Must be in CONNECTED state
    if (ancs_ctx.state != ANCS_STATE_CONNECTED) {
        return;
    }
    
    // Check prerequisites
    if (!ancs_ctx.mtu_exchange_complete) {
        PR_DEBUG("ANCS: Waiting for MTU exchange to complete");
        return;
    }
    
    // Encryption is assumed ready - iOS initiates pairing automatically
    // If discovery fails, we'll know encryption wasn't ready yet
    PR_DEBUG("ANCS: Prerequisites met, starting service discovery");
    ancs_start_service_discovery();
}

/**
 * @brief Start service discovery
 * 
 * Internal function that actually initiates the service discovery procedure.
 */
static void ancs_start_service_discovery(void)
{
    if (ancs_ctx.state != ANCS_STATE_CONNECTED) {
        PR_DEBUG("ANCS: Cannot start discovery, not in connected state");
        return;
    }
    
    PR_DEBUG("ANCS: Starting ANCS service discovery");
    ancs_ctx.state = ANCS_STATE_DISCOVERING_SERVICE;
    ancs_ctx.service_found = false;
    
    int rc = ble_gattc_disc_svc_by_uuid(ancs_ctx.conn_handle,
                                         &ancs_service_uuid.u,
                                         ancs_svc_disc_cb,
                                         NULL);
    if (rc != 0) {
        PR_DEBUG("ANCS: Failed to start service discovery, rc=%d (encryption may not be ready)", rc);
        // Return to CONNECTED state - discovery will be retried when encryption completes
        // (encryption completion may trigger another discovery attempt)
        ancs_ctx.state = ANCS_STATE_CONNECTED;
    }
}

OPERATE_RET ancs_init(ancs_notification_callback_t notification_cb,
                      ancs_notification_data_callback_t data_cb)
{
    if (!notification_cb || !data_cb) {
        return OPRT_INVALID_PARM;
    }
    
    memset(&ancs_ctx, 0, sizeof(ancs_ctx));
    ancs_ctx.state = ANCS_STATE_DISCONNECTED;
    ancs_ctx.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ancs_ctx.notification_cb = notification_cb;
    ancs_ctx.data_cb = data_cb;
    
    PR_DEBUG("ANCS: Initialized with security configuration (iOS will initiate pairing)");
    return OPRT_OK;
}

void ancs_handle_ble_event(TAL_BLE_EVT_PARAMS_T *p_event)
{
    if (!p_event) {
        return;
    }
    
    switch (p_event->type) {
        case TAL_BLE_EVT_PERIPHERAL_CONNECT: {
            if (p_event->ble_event.connect.result == 0) {
                PR_DEBUG("ANCS: iPhone connected, conn_handle=%d",
                         p_event->ble_event.connect.peer.conn_handle);
                ancs_ctx.conn_handle = p_event->ble_event.connect.peer.conn_handle;
                ancs_ctx.state = ANCS_STATE_CONNECTED;
                ancs_ctx.mtu_exchange_complete = false;
                ancs_ctx.encryption_ready = false;  // Will be inferred when discovery succeeds
                
                // ANCS requires pairing/encryption before service discovery
                // With security configuration enabled, iOS will initiate pairing automatically
                PR_DEBUG("ANCS: iPhone connected - waiting for MTU exchange and encryption");
                
                // State-based: Check if we can start discovery (will wait for prerequisites)
                ancs_check_and_start_discovery();
            }
            break;
        }
        
        case TAL_BLE_EVT_MTU_RSP: {
            if (p_event->ble_event.exchange_mtu.conn_handle == ancs_ctx.conn_handle) {
                PR_DEBUG("ANCS: MTU exchange complete, mtu=%d", 
                         p_event->ble_event.exchange_mtu.mtu);
                ancs_ctx.mtu_exchange_complete = true;
                
                // State-based: Check if all prerequisites are met and start discovery
                ancs_check_and_start_discovery();
            }
            break;
        }
        
        case TAL_BLE_EVT_DISCONNECT: {
            if (p_event->ble_event.disconnect.peer.conn_handle == ancs_ctx.conn_handle) {
                PR_DEBUG("ANCS: iPhone disconnected");
                
                ancs_ctx.state = ANCS_STATE_DISCONNECTED;
                ancs_ctx.conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ancs_ctx.service_start_handle = 0;
                ancs_ctx.service_end_handle = 0;
                ancs_ctx.notification_source_handle = 0;
                ancs_ctx.control_point_handle = 0;
                ancs_ctx.data_source_handle = 0;
                ancs_ctx.notification_source_cccd_handle = 0;
                ancs_ctx.data_source_cccd_handle = 0;
                ancs_ctx.mtu_exchange_complete = false;
                ancs_ctx.encryption_ready = false;
            }
            break;
        }
        
        case TAL_BLE_EVT_NOTIFY_RX: {
            // Handle notification data from iPhone
            // Note: Since we subscribe directly via nimble, notifications may come through
            // nimble's callback mechanism rather than TAL events. This handler is provided
            // as a fallback and may need adjustment based on actual stack behavior.
            if (p_event->ble_event.data_report.peer.conn_handle == ancs_ctx.conn_handle) {
                if (ancs_ctx.state == ANCS_STATE_READY) {
                    TAL_BLE_DATA_T *data = &p_event->ble_event.data_report.report;
                    
                    // Differentiate between Notification Source and Data Source by format:
                    // - Notification Source: Always 8 bytes
                    // - Data Source: Variable length, starts with command ID
                    if (data->len == 8) {
                        // Likely Notification Source (exactly 8 bytes)
                        ancs_parse_notification_source(data->p_data, data->len);
                    } else if (data->len > 8) {
                        // Likely Data Source (variable length)
                        ancs_parse_data_source(data->p_data, data->len);
                    }
                }
            }
            break;
        }
        
        default:
            break;
    }
}

OPERATE_RET ancs_get_notification_attributes(uint32_t notification_uid,
                                              uint8_t *attribute_ids,
                                              uint8_t attribute_count)
{
    if (ancs_ctx.state != ANCS_STATE_READY) {
        return OPRT_INVALID_PARM;
    }
    
    if (!attribute_ids || attribute_count == 0 || attribute_count > 8) {
        return OPRT_INVALID_PARM;
    }
    
    // Build command packet
    // Format: Command ID (1) + Notification UID (4) + Attribute ID (1) per attribute
    uint8_t cmd[1 + 4 + 8];
    cmd[0] = ANCS_COMMAND_ID_GET_NOTIFICATION_ATTRIBUTES;
    cmd[1] = notification_uid & 0xFF;
    cmd[2] = (notification_uid >> 8) & 0xFF;
    cmd[3] = (notification_uid >> 16) & 0xFF;
    cmd[4] = (notification_uid >> 24) & 0xFF;
    
    for (uint8_t i = 0; i < attribute_count; i++) {
        cmd[5 + i] = attribute_ids[i];
    }
    
    int rc = ble_gattc_write_no_rsp_flat(ancs_ctx.conn_handle,
                                          ancs_ctx.control_point_handle,
                                          cmd,
                                          5 + attribute_count);
    if (rc != 0) {
        PR_DEBUG("ANCS: Failed to send get attributes command, rc=%d", rc);
        return OPRT_COM_ERROR;
    }
    
    PR_DEBUG("ANCS: Requested %d attributes for notification 0x%08x", attribute_count, notification_uid);
    return OPRT_OK;
}

OPERATE_RET ancs_perform_notification_action(uint32_t notification_uid,
                                              uint8_t action_id)
{
    if (ancs_ctx.state != ANCS_STATE_READY) {
        return OPRT_INVALID_PARM;
    }
    
    if (action_id > 1) {
        return OPRT_INVALID_PARM;
    }
    
    // Build command packet
    // Format: Command ID (1) + Notification UID (4) + Action ID (1)
    uint8_t cmd[6];
    cmd[0] = ANCS_COMMAND_ID_PERFORM_NOTIFICATION_ACTION;
    cmd[1] = notification_uid & 0xFF;
    cmd[2] = (notification_uid >> 8) & 0xFF;
    cmd[3] = (notification_uid >> 16) & 0xFF;
    cmd[4] = (notification_uid >> 24) & 0xFF;
    cmd[5] = action_id;
    
    int rc = ble_gattc_write_no_rsp_flat(ancs_ctx.conn_handle,
                                          ancs_ctx.control_point_handle,
                                          cmd,
                                          sizeof(cmd));
    if (rc != 0) {
        PR_DEBUG("ANCS: Failed to send action command, rc=%d", rc);
        return OPRT_COM_ERROR;
    }
    
    PR_DEBUG("ANCS: Performed action %d on notification 0x%08x", action_id, notification_uid);
    return OPRT_OK;
}

bool ancs_is_connected(void)
{
    return ancs_ctx.state == ANCS_STATE_READY;
}

void ancs_deinit(void)
{
    memset(&ancs_ctx, 0, sizeof(ancs_ctx));
    ancs_ctx.state = ANCS_STATE_DISCONNECTED;
    ancs_ctx.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    PR_DEBUG("ANCS: Deinitialized");
}

