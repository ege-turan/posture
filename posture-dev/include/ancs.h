/**
 * @file ancs.h
 * @brief Apple Notification Center Service (ANCS) client implementation
 *
 * This module implements ANCS client functionality, allowing the board to
 * receive notifications from an iPhone. The board acts as a BLE peripheral
 * (for connection role) and GATT client (to discover and interact with ANCS
 * service on the iPhone, which acts as BLE central and GATT server).
 *
 * ANCS allows accessories to receive notifications from iOS devices including:
 * - New notification alerts
 * - Modified notifications
 * - Removed notifications
 * - Detailed notification information (title, message, app name, etc.)
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef ANCS_H
#define ANCS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "tal_bluetooth.h"
#include "tal_bluetooth_def.h"

/**
 * @brief ANCS Service and Characteristic UUIDs
 */
#define ANCS_SERVICE_UUID                 0x7905F431, 0xB5CE, 0x4E99, 0xA40F, 0x4B1E122D00D0
#define ANCS_NOTIFICATION_SOURCE_UUID     0x9FBF120D, 0x6301, 0x42D9, 0x8C58, 0x25E699A21DBD
#define ANCS_CONTROL_POINT_UUID           0x69D1D8F3, 0x45E1, 0x49A8, 0x9821, 0x9BBDFDAAD9D9
#define ANCS_DATA_SOURCE_UUID             0x22EAC6E9, 0x24D6, 0x4BB5, 0xBE44, 0xB36ACE7C7BFB

/**
 * @brief ANCS Notification Event IDs
 */
typedef enum {
    ANCS_EVENT_ID_NOTIFICATION_ADDED      = 0,
    ANCS_EVENT_ID_NOTIFICATION_MODIFIED   = 1,
    ANCS_EVENT_ID_NOTIFICATION_REMOVED    = 2,
} ancs_event_id_t;

/**
 * @brief ANCS Notification Categories
 */
typedef enum {
    ANCS_CATEGORY_ID_OTHER                = 0,
    ANCS_CATEGORY_ID_INCOMING_CALL        = 1,
    ANCS_CATEGORY_ID_MISSED_CALL          = 2,
    ANCS_CATEGORY_ID_VOICEMAIL            = 3,
    ANCS_CATEGORY_ID_SOCIAL               = 4,
    ANCS_CATEGORY_ID_SCHEDULE             = 5,
    ANCS_CATEGORY_ID_EMAIL                = 6,
    ANCS_CATEGORY_ID_NEWS                 = 7,
    ANCS_CATEGORY_ID_HEALTH_AND_FITNESS   = 8,
    ANCS_CATEGORY_ID_BUSINESS_AND_FINANCE = 9,
    ANCS_CATEGORY_ID_LOCATION             = 10,
    ANCS_CATEGORY_ID_ENTERTAINMENT        = 11,
} ancs_category_id_t;

/**
 * @brief ANCS Notification Event Flags
 */
typedef enum {
    ANCS_EVENT_FLAG_SILENT                = 0x01,
    ANCS_EVENT_FLAG_IMPORTANT             = 0x02,
    ANCS_EVENT_FLAG_PRE_EXISTING          = 0x04,
    ANCS_EVENT_FLAG_POSITIVE_ACTION       = 0x08,
    ANCS_EVENT_FLAG_NEGATIVE_ACTION       = 0x10,
} ancs_event_flag_t;

/**
 * @brief ANCS Command IDs for Control Point
 */
typedef enum {
    ANCS_COMMAND_ID_GET_NOTIFICATION_ATTRIBUTES = 0,
    ANCS_COMMAND_ID_GET_APP_ATTRIBUTES          = 1,
    ANCS_COMMAND_ID_PERFORM_NOTIFICATION_ACTION = 2,
} ancs_command_id_t;

/**
 * @brief ANCS Notification Attributes
 */
typedef enum {
    ANCS_NOTIFICATION_ATTRIBUTE_ID_APP_IDENTIFIER = 0,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_TITLE          = 1,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_SUBTITLE       = 2,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_MESSAGE        = 3,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_MESSAGE_SIZE   = 4,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_DATE           = 5,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_POSITIVE_ACTION_LABEL  = 6,
    ANCS_NOTIFICATION_ATTRIBUTE_ID_NEGATIVE_ACTION_LABEL  = 7,
} ancs_notification_attribute_id_t;

/**
 * @brief ANCS Notification Event structure
 */
typedef struct {
    uint8_t event_id;          ///< Event ID (added, modified, removed)
    uint8_t event_flags;       ///< Event flags
    uint8_t category_id;       ///< Category ID
    uint8_t category_count;    ///< Number of notifications in category
    uint32_t notification_uid; ///< Notification UID (unique identifier)
} ancs_notification_event_t;

/**
 * @brief ANCS notification callback function type
 *
 * @param event_id Event type (added, modified, removed)
 * @param category_id Category of the notification
 * @param notification_uid Unique identifier for the notification
 * @param flags Event flags
 */
typedef void (*ancs_notification_callback_t)(uint8_t event_id,
                                              uint8_t category_id,
                                              uint32_t notification_uid,
                                              uint8_t flags);

/**
 * @brief ANCS notification data callback function type
 *
 * Called when notification details are received from iPhone
 *
 * @param notification_uid Notification UID
 * @param attribute_id Attribute ID (title, message, etc.)
 * @param data Attribute data (UTF-8 string)
 * @param data_len Length of data
 */
typedef void (*ancs_notification_data_callback_t)(uint32_t notification_uid,
                                                   uint8_t attribute_id,
                                                   const uint8_t *data,
                                                   uint16_t data_len);

/**
 * @brief Initialize ANCS client
 *
 * This function should be called after BLE stack is initialized.
 * It sets up the ANCS client to handle incoming connections from iPhone.
 *
 * @param notification_cb Callback for notification events (add/modify/remove)
 * @param data_cb Callback for notification detail data
 * @return OPERATE_RET SUCCESS on success, error code otherwise
 */
OPERATE_RET ancs_init(ancs_notification_callback_t notification_cb,
                      ancs_notification_data_callback_t data_cb);

/**
 * @brief Handle BLE event for ANCS
 *
 * This function should be called from the main BLE event callback
 * to handle ANCS-related events (connections, notifications, etc.)
 *
 * @param p_event BLE event parameters
 */
void ancs_handle_ble_event(TAL_BLE_EVT_PARAMS_T *p_event);

/**
 * @brief Request notification attributes from iPhone
 *
 * Requests detailed information about a notification (title, message, etc.)
 *
 * @param notification_uid Notification UID
 * @param attribute_ids Array of attribute IDs to request
 * @param attribute_count Number of attributes to request (max 8)
 * @return OPERATE_RET SUCCESS on success, error code otherwise
 */
OPERATE_RET ancs_get_notification_attributes(uint32_t notification_uid,
                                              uint8_t *attribute_ids,
                                              uint8_t attribute_count);

/**
 * @brief Perform notification action
 *
 * Performs an action on a notification (e.g., dismiss, open app)
 *
 * @param notification_uid Notification UID
 * @param action_id Action ID (0 = positive, 1 = negative)
 * @return OPERATE_RET SUCCESS on success, error code otherwise
 */
OPERATE_RET ancs_perform_notification_action(uint32_t notification_uid,
                                              uint8_t action_id);

/**
 * @brief Get connection status
 *
 * @return true if connected to iPhone with ANCS service available
 */
bool ancs_is_connected(void);

/**
 * @brief Deinitialize ANCS client
 */
void ancs_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ANCS_H */
