/**
 * @file main.c
 * @brief Main application entry point
 *
 * This file contains the main application logic for the Tuya IoT project.
 * It demonstrates ANCS (Apple Notification Center Service) integration to
 * receive and display notifications from an iPhone.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "tkl_output.h"
#include "tal_cli.h"
#include "tal_bluetooth.h"
#include "tal_bluetooth_def.h"
#include "ancs.h"

#include <string.h>
#include <stdint.h>

// Storage for notification attributes
typedef struct {
    uint32_t uid;
    char app_identifier[256];
    char title[256];
    char message[512];
    bool has_app_id;
    bool has_title;
    bool has_message;
} NotificationData_t;

static NotificationData_t current_notification = {0};

// Static advertising parameters
static const TAL_BLE_ADV_PARAMS_T default_adv_params = {
    .adv_interval_min = 30,
    .adv_interval_max = 60,
    .adv_type = TAL_BLE_ADV_TYPE_CS_UNDIR,
    .direct_addr = {TAL_BLE_ADDR_TYPE_PUBLIC, {0}}
};

/**
 * @brief BLE event callback
 *
 * Handles BLE events and forwards them to the ANCS module.
 */
static void ble_event_callback(TAL_BLE_EVT_PARAMS_T *p_event)
{
    PR_DEBUG("BLE event: %d", p_event->type);
    
    // Forward events to ANCS module
    ancs_handle_ble_event(p_event);
    
    // Handle BLE stack initialization and advertising
    switch (p_event->type) {
        case TAL_BLE_STACK_INIT: {
            PR_NOTICE("BLE stack initialized");
            if (p_event->ble_event.init == 0) {
                // Set up advertising data for iPhone to discover us
                uint8_t adv_data[] = {
                    0x02, 0x01, 0x06,  // Flags: LE General Discoverable
                    0x05, 0x12, 0x18, 0x0F, 0x18, 0x0A,  // Complete list of 16-bit service UUIDs
                    // Add more advertising data as needed
                };
                
                uint8_t scan_rsp_data[] = {
                    0x0C, 0x09, 'A', 'N', 'C', 'S', ' ', 'D', 'e', 'v', 'i', 'c', 'e',  // Complete local name
                };
                
                TAL_BLE_DATA_T adv;
                TAL_BLE_DATA_T scan_rsp;
                adv.p_data = adv_data;
                adv.len = sizeof(adv_data);
                scan_rsp.p_data = scan_rsp_data;
                scan_rsp.len = sizeof(scan_rsp_data);
                
                tal_ble_advertising_data_set(&adv, &scan_rsp);
                
                // Start advertising so iPhone can connect
                tal_system_sleep(500);
                tal_ble_advertising_start(&default_adv_params);
                PR_NOTICE("Started BLE advertising - waiting for iPhone to connect...");
            }
            break;
        }
        
        case TAL_BLE_EVT_PERIPHERAL_CONNECT: {
            if (p_event->ble_event.connect.result == 0) {
                PR_NOTICE("Device connected - ANCS discovery will begin");
            }
            break;
        }
        
        case TAL_BLE_EVT_DISCONNECT: {
            PR_NOTICE("Device disconnected - restarting advertising");
            tal_system_sleep(500);
            tal_ble_advertising_start(&default_adv_params);
            // Clear current notification
            memset(&current_notification, 0, sizeof(current_notification));
            break;
        }
        
        default:
            break;
    }
}

/**
 * @brief ANCS notification callback
 *
 * Called when a notification is added, modified, or removed.
 */
static void ancs_notification_cb(uint8_t event_id,
                                  uint8_t category_id,
                                  uint32_t notification_uid,
                                  uint8_t flags)
{
    const char* event_str[] = {"ADDED", "MODIFIED", "REMOVED"};
    const char* category_str[] = {
        "OTHER", "INCOMING_CALL", "MISSED_CALL", "VOICEMAIL",
        "SOCIAL", "SCHEDULE", "EMAIL", "NEWS",
        "HEALTH_AND_FITNESS", "BUSINESS_AND_FINANCE", "LOCATION", "ENTERTAINMENT"
    };
    
    if (event_id <= ANCS_EVENT_ID_NOTIFICATION_REMOVED) {
        PR_NOTICE("========================================");
        PR_NOTICE("ANCS Notification %s", event_str[event_id]);
        PR_NOTICE("  Category: %s (%d)", 
                  category_id < 12 ? category_str[category_id] : "UNKNOWN", 
                  category_id);
        PR_NOTICE("  UID: 0x%08X", notification_uid);
        PR_NOTICE("  Flags: 0x%02X", flags);
        PR_NOTICE("========================================");
    }
    
    // Request notification details for added/modified notifications
    if (event_id == ANCS_EVENT_ID_NOTIFICATION_ADDED || 
        event_id == ANCS_EVENT_ID_NOTIFICATION_MODIFIED) {
        // Clear previous notification data
        memset(&current_notification, 0, sizeof(current_notification));
        current_notification.uid = notification_uid;
        
        // Request app identifier, title, and message
        uint8_t attributes[] = {
            ANCS_NOTIFICATION_ATTRIBUTE_ID_APP_IDENTIFIER,
            ANCS_NOTIFICATION_ATTRIBUTE_ID_TITLE,
            ANCS_NOTIFICATION_ATTRIBUTE_ID_MESSAGE,
        };
        
        OPERATE_RET ret = ancs_get_notification_attributes(notification_uid, 
                                                           attributes, 
                                                           3);  // Number of attributes
        if (ret != OPRT_OK) {
            PR_DEBUG("Failed to request notification attributes: %d", ret);
        } else {
            PR_DEBUG("Requested notification attributes for UID 0x%08X", notification_uid);
        }
    }
}

/**
 * @brief ANCS data callback
 *
 * Called when notification attribute data is received.
 */
static void ancs_data_cb(uint32_t notification_uid,
                         uint8_t attribute_id,
                         const uint8_t *data,
                         uint16_t data_len)
{
    // Only process data for the current notification
    if (notification_uid != current_notification.uid) {
        return;
    }
    
    // Ensure we don't overflow buffers
    size_t max_len;
    char* target;
    
    switch (attribute_id) {
        case ANCS_NOTIFICATION_ATTRIBUTE_ID_APP_IDENTIFIER:
            target = current_notification.app_identifier;
            max_len = sizeof(current_notification.app_identifier) - 1;
            current_notification.has_app_id = true;
            break;
            
        case ANCS_NOTIFICATION_ATTRIBUTE_ID_TITLE:
            target = current_notification.title;
            max_len = sizeof(current_notification.title) - 1;
            current_notification.has_title = true;
            break;
            
        case ANCS_NOTIFICATION_ATTRIBUTE_ID_MESSAGE:
            target = current_notification.message;
            max_len = sizeof(current_notification.message) - 1;
            current_notification.has_message = true;
            break;
            
        default:
            return; // Ignore other attributes for now
    }
    
    // Copy data (ANCS data is UTF-8)
    size_t copy_len = (data_len < max_len) ? data_len : max_len;
    memcpy(target, data, copy_len);
    target[copy_len] = '\0';
    
    // Print received attribute
    const char* attr_name;
    switch (attribute_id) {
        case ANCS_NOTIFICATION_ATTRIBUTE_ID_APP_IDENTIFIER:
            attr_name = "App Identifier";
            break;
        case ANCS_NOTIFICATION_ATTRIBUTE_ID_TITLE:
            attr_name = "Title";
            break;
        case ANCS_NOTIFICATION_ATTRIBUTE_ID_MESSAGE:
            attr_name = "Message";
            break;
        default:
            attr_name = "Unknown";
    }
    
    PR_NOTICE("  %s: %s", attr_name, target);
    
    // When we have all three attributes, print summary
    if (current_notification.has_app_id && 
        current_notification.has_title && 
        current_notification.has_message) {
        PR_NOTICE("--- Complete Notification ---");
        PR_NOTICE("  App: %s", current_notification.app_identifier);
        PR_NOTICE("  Title: %s", current_notification.title);
        PR_NOTICE("  Message: %s", current_notification.message);
        PR_NOTICE("--- End Notification ---");
    }
}

/**
 * @brief Main application logic
 *
 * Initializes the Tuya logging system, BLE stack, and ANCS module.
 */
void user_main(void)
{
    OPERATE_RET ret;
    
    // Initialize logging
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    
    PR_NOTICE("========================================");
    PR_NOTICE("ANCS Notification Receiver");
    PR_NOTICE("========================================");
    
    // Initialize KV storage (required by Tuya SDK)
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "vmlkasdh93dlvlcy",
        .key = "dflfuap134ddlduq",
    });
    
    // Initialize software timer and work queue
    tal_sw_timer_init();
    tal_workq_init();
    
    // Initialize ANCS module
    ret = ancs_init(ancs_notification_cb, ancs_data_cb);
    if (ret != OPRT_OK) {
        PR_NOTICE("Failed to initialize ANCS: %d", ret);
        return;
    }
    PR_NOTICE("ANCS module initialized");
    
    // Initialize BLE in peripheral mode
    ret = tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, ble_event_callback);
    if (ret != OPRT_OK) {
        PR_NOTICE("Failed to initialize BLE: %d", ret);
        return;
    }
    PR_NOTICE("BLE initialized - waiting for stack ready...");
    
    // Main loop - just sleep, events are handled in callbacks
    while (1) {
        tal_system_sleep(1000);
        
        // Periodic status check
        static uint32_t status_counter = 0;
        if (++status_counter % 10 == 0) {  // Every 10 seconds
            if (ancs_is_connected()) {
                PR_DEBUG("Status: Connected to iPhone with ANCS ready");
            } else {
                PR_DEBUG("Status: Waiting for iPhone connection...");
            }
        }
    }
}

/**
 * @brief Main entry point for Linux systems
 *
 * @param argc Number of command line arguments
 * @param argv Command line arguments array
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    (void)argc;  // Suppress unused parameter warning
    (void)argv;  // Suppress unused parameter warning
    user_main();
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief Task thread function
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
static void tuya_app_thread(void *arg)
{
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

/**
 * @brief Tuya application main entry point
 *
 * This function is called by the Tuya SDK to start the application.
 * It creates and starts a thread to run the main application logic.
 */
void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif

