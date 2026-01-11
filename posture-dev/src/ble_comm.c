/**
 * @file ble_comm.c
 * @brief Bluetooth Low Energy communication implementation (temporary stub)
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 * 
 * TODO: Implement actual BLE functionality for phone notifications
 */

#include "ble_comm.h"
#include "tal_api.h"
#include "tkl_output.h"

static void (*s_notification_callback)(const char* notification_type, const char* message, void* user_data) = NULL;
static void* s_user_data = NULL;

OPERATE_RET ble_comm_init(void)
{
    PR_NOTICE("BLE communication initialized (stub)");
    // TODO: Initialize BLE stack, GATT services, etc.
    return OPRT_OK;
}

OPERATE_RET ble_comm_start(void)
{
    PR_NOTICE("BLE communication started (stub)");
    // TODO: Start advertising, enable notifications, etc.
    return OPRT_OK;
}

OPERATE_RET ble_comm_stop(void)
{
    PR_NOTICE("BLE communication stopped (stub)");
    // TODO: Stop advertising, disable notifications, etc.
    return OPRT_OK;
}

OPERATE_RET ble_comm_deinit(void)
{
    PR_NOTICE("BLE communication deinitialized (stub)");
    s_notification_callback = NULL;
    s_user_data = NULL;
    // TODO: Cleanup BLE stack, free resources, etc.
    return OPRT_OK;
}

OPERATE_RET ble_comm_register_callback(void (*callback)(const char* notification_type, const char* message, void* user_data), void* user_data)
{
    s_notification_callback = callback;
    s_user_data = user_data;
    PR_NOTICE("BLE callback registered (stub)");
    // TODO: Register actual BLE notification handlers
    return OPRT_OK;
}
