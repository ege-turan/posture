/**
 * @file posture_detect.h
 * @brief Posture detection module interface
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef POSTURE_DETECT_H
#define POSTURE_DETECT_H

#include "tuya_cloud_types.h"
#include "inference.h"
#include "camera.h"

/**
 * @brief Camera frame callback for posture detection
 * 
 * This callback processes camera frames through inference and updates
 * posture detection state. Register this with camera_start().
 * 
 * @param yuv422_data Pointer to YUV422 frame data (UYVY format)
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param user_data User context pointer (can be NULL)
 */
void posture_frame_callback(uint8_t* yuv422_data, int width, int height, void* user_data);

/**
 * @brief Get the last detected posture status
 * 
 * @param angle_out Output parameter for neck angle in degrees (can be NULL)
 * @return int 0=BAD, 1=GOOD, 2=UNDETECTED
 */
int posture_get_status(float* angle_out);

/**
 * @brief Get the last pose result
 * 
 * @param pose_out Output parameter for pose result
 * @return OPERATE_RET OPRT_OK if pose is available
 */
OPERATE_RET posture_get_pose(pose_result_t* pose_out);

/**
 * @brief Initialize queues and worker threads
 * 
 * Must be called before camera_start() to set up the queue-based architecture.
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET posture_detect_queue_init(void);

/**
 * @brief Start all worker threads (inference, display, BLE)
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET posture_detect_threads_start(void);

/**
 * @brief Deinitialize queues and stop worker threads
 * 
 * Should be called during cleanup to properly stop threads and free resources.
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET posture_detect_queue_deinit(void);

#endif // POSTURE_DETECT_H