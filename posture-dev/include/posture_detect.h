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
 * @return true if posture is good, false otherwise
 */
bool posture_get_status(float* angle_out);

/**
 * @brief Get the last pose result
 * 
 * @param pose_out Output parameter for pose result
 * @return OPERATE_RET OPRT_OK if pose is available
 */
OPERATE_RET posture_get_pose(pose_result_t* pose_out);

#endif // POSTURE_DETECT_H