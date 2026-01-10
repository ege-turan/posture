/**
 * @file camera.h
 * @brief Camera module interface - simplified high-level API
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frame callback function type
 * Called when a new camera frame is available
 * 
 * @param yuv422_data Pointer to YUV422 frame data (UYVY format)
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param user_data User-provided context pointer
 */
typedef void (*camera_frame_callback_t)(uint8_t* yuv422_data, int width, int height, void* user_data);

/**
 * @brief Initialize camera and display system
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET camera_init(void);

/**
 * @brief Start camera capture with frame callback
 * 
 * @param frame_cb Callback function called for each frame (NULL to disable inference)
 * @param user_data User context pointer passed to callback (can be NULL)
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET camera_start(camera_frame_callback_t frame_cb, void* user_data);

/**
 * @brief Stop camera capture
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET camera_stop(void);

/**
 * @brief Deinitialize camera and display system
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET camera_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_H
