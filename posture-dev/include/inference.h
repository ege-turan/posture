/**
 * @file inference.h
 * @brief MoveNet inference module interface
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef INFERENCE_H
#define INFERENCE_H

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// MoveNet model input dimensions (adjust based on your model)
#define MOVENET_INPUT_WIDTH  192
#define MOVENET_INPUT_HEIGHT 192
#define MOVENET_INPUT_CHANNELS 3
#define MOVENET_KEYPOINT_COUNT 17  // Standard MoveNet output: 17 keypoints

/**
 * @brief 2D keypoint structure (x, y coordinates and confidence)
 * 
 * Coordinates are normalized to [0.0, 1.0] relative to the model input frame (192x192).
 * To convert to original image pixel coordinates, multiply by original image dimensions.
 */
typedef struct {
    float x;        // X coordinate (normalized 0.0-1.0, relative to 192x192 input)
    float y;        // Y coordinate (normalized 0.0-1.0, relative to 192x192 input)
    float score;    // Confidence score (0.0-1.0)
} keypoint_t;

/**
 * @brief Pose detection result containing all keypoints
 */
typedef struct {
    keypoint_t keypoints[MOVENET_KEYPOINT_COUNT];  // 17 keypoints
    float overall_score;  // Overall pose confidence
} pose_result_t;

/**
 * @brief Inference configuration
 */
typedef struct {
    bool enable_visualization;  // Overlay keypoints on output image
    float min_confidence;       // Minimum keypoint confidence threshold (0.0-1.0)
} inference_config_t;

/**
 * @brief Initialize MoveNet inference engine
 * 
 * @param config Inference configuration (can be NULL for defaults)
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET inference_init(const inference_config_t* config);

/**
 * @brief Process a camera frame through MoveNet
 * 
 * This function:
 * 1. Converts YUV422 to RGB888
 * 2. Resizes to 192x192 (nearest-neighbor interpolation)
 * 3. Converts to uint8 [0, 255] format (MoveNet input requirement)
 * 4. Runs inference
 * 5. Extracts 17 keypoints with normalized coordinates [0.0, 1.0]
 * 
 * Note: Keypoint coordinates are normalized relative to the 192x192 model input.
 * To convert to original image pixel coordinates:
 *   pixel_x = keypoint.x * original_width
 *   pixel_y = keypoint.y * original_height
 * 
 * @param yuv422_data Pointer to YUV422 input frame (UYVY format)
 * @param input_width Original frame width (used for coordinate conversion if needed)
 * @param input_height Original frame height (used for coordinate conversion if needed)
 * @param result Output pose detection result (coordinates normalized to [0.0, 1.0])
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET inference_process_frame(uint8_t* yuv422_data, int input_width, int input_height, 
                                     pose_result_t* result);

/**
 * @brief Get the latest inference result (thread-safe)
 * 
 * @param result Output buffer for pose result
 * @return OPERATE_RET OPRT_OK if result available, error otherwise
 */
OPERATE_RET inference_get_latest_result(pose_result_t* result);

/**
 * @brief Deinitialize inference engine and free resources
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET inference_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // INFERENCE_H
