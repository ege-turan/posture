/**
 * @file ble_comm.h
 * @brief Bluetooth Low Energy communication interface (temporary stub)
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 * 
 * TODO: Implement actual BLE functionality for phone notifications
 */

#ifndef BLE_COMM_H
#define BLE_COMM_H

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Bluetooth Low Energy communication
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET ble_comm_init(void);

/**
 * @brief Start BLE advertising and listening for notifications
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET ble_comm_start(void);

/**
 * @brief Stop BLE communication
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET ble_comm_stop(void);

/**
 * @brief Deinitialize BLE communication
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET ble_comm_deinit(void);

/**
 * @brief Register callback for received notifications
 * 
 * @param callback Function to call when notification received
 * @param user_data User context pointer
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET ble_comm_register_callback(void (*callback)(const char* notification_type, const char* message, void* user_data), void* user_data);

#ifdef __cplusplus
}
#endif

#endif // BLE_COMM_H
