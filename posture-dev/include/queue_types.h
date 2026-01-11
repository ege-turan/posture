/**
 * @file queue_types.h
 * @brief Queue data structures for inter-thread communication
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef QUEUE_TYPES_H
#define QUEUE_TYPES_H

#include "tuya_cloud_types.h"
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frame queue item structure
 * Contains camera frame data to be processed by inference worker
 */
typedef struct {
    uint8_t* frame_data;      // YUV422 frame data (allocated, must be freed by consumer)
    int width;                // Frame width in pixels
    int height;               // Frame height in pixels
    uint64_t timestamp_ms;    // Frame timestamp (milliseconds)
} frame_queue_item_t;

/**
 * @brief Notification type enumeration
 */
typedef enum {
    NOTIFY_TYPE_PHONE_CALL = 0,        // Phone call notification from Bluetooth
    NOTIFY_TYPE_MESSAGE,               // Text message notification from Bluetooth
    NOTIFY_TYPE_POSTURE_WARNING,       // Bad posture warning
    NOTIFY_TYPE_POSTURE_GOOD,          // Posture improved notification (optional)
    NOTIFY_TYPE_SYSTEM_INFO            // System information message
} notification_type_t;

/**
 * @brief Notification queue item structure
 * Contains notification messages for display
 */
typedef struct {
    notification_type_t type;
    char message[128];                 // Notification message text
    uint32_t duration_ms;              // Display duration in milliseconds (0 = default)
    int priority;                      // Priority (higher = more urgent)
} notification_queue_item_t;

#ifdef __cplusplus
}
#endif

#endif // QUEUE_TYPES_H
