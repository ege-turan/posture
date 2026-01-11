/**
 * @file posture_detect.cpp
 * @brief MoveNet-based posture detection with on-screen display (Tuya SDK)
 *
 * Implements real-time posture detection using MoveNet SinglePose Lightning INT8.
 * Draws skeleton, keypoints, neck angle, and posture status on display.
 *
 * Copyright (c) 2021-2025 Tuya Inc.
 */

#include "inference.h"
#include "camera.h"

#include "tal_api.h"
#include "tkl_output.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>

/* =========================================================
 *                Constants & configuration
 * ========================================================= */
#define MOVENET_KP_NUM 17
#define SCORE_THRESH  0.2f
#define GOOD_NECK_ANGLE_DEG 15.0f

/* =========================================================
 *                Skeleton edges (MoveNet)
 * ========================================================= */
static const uint8_t EDGES[][2] = {
    {0,1},{0,2},{1,3},{2,4},
    {0,5},{0,6},
    {5,7},{7,9},
    {6,8},{8,10},
    {5,11},{6,12},
    {11,12},
    {11,13},{13,15},
    {12,14},{14,16}
};

/* =========================================================
 *                Pose state
 * ========================================================= */
static pose_result_t g_last_pose = {0};
static float g_neck_angle = 0.0f;
static int g_posture_status = 0;  // 0=BAD, 1=GOOD, 2=UNDETECTED

/* =========================================================
 *                Utility math
 * ========================================================= */
static float deg_atan2(float dy, float dx)
{
    return std::atan2(dy, dx) * 57.29578f + 90;
}

/* =========================================================
 *                Posture detection
 * ========================================================= */
/**
 * @brief Determine posture status based on keypoints
 * 
 * @param p Pointer to pose result
 * @param angle_out Output parameter for neck angle in degrees
 * @return int 0=BAD, 1=GOOD, 2=UNDETECTED
 */
static int posture_good(const pose_result_t* p, float& angle_out)
{
    if (p == nullptr) {
        angle_out = 0.0f;
        return 2;  // UNDETECTED
    }

    const auto& nose = p->keypoints[0];
    const auto& ls   = p->keypoints[5];
    const auto& rs   = p->keypoints[6];

    // Check if any keypoint has low confidence (UNDETECTED)
    if (nose.score < SCORE_THRESH ||
        ls.score   < SCORE_THRESH ||
        rs.score   < SCORE_THRESH) {
        angle_out = 0.0f;
        return 2;  // UNDETECTED
    }

    // Calculate neck angle
    float sx = (ls.x + rs.x) * 0.5f;
    float sy = (ls.y + rs.y) * 0.5f;

    angle_out = deg_atan2(nose.y - sy, nose.x - sx);
    
    // Return 1=GOOD if angle is within threshold, 0=BAD otherwise
    if (std::fabs(angle_out) < GOOD_NECK_ANGLE_DEG) {
        return 1;  // GOOD
    } else {
        return 0;  // BAD
    }
}


/* =========================================================
 *                Camera callback
 * ========================================================= */
void posture_frame_callback(uint8_t* yuv422_data, int width, int height, void* user_data)
{
    (void)user_data;

    pose_result_t pose{};
    float neck_angle = 0;
    int posture_status = 2;  // Default to UNDETECTED

    // Process frame through MoveNet inference (using inference.cpp)
    OPERATE_RET ret = inference_process_frame(yuv422_data, width, height, &pose);
    if (ret == OPRT_OK) {
        posture_status = posture_good(&pose, neck_angle);
        
        // Print only keypoints 0, 5, 6
        PR_NOTICE("=== MoveNet Keypoints (0, 5, 6) ===");
        PR_NOTICE("Overall score: %.3f", pose.overall_score);
        const int kp_indices[] = {0, 5, 6};
        for (int j = 0; j < 3; j++) {
            int i = kp_indices[j];
            PR_NOTICE("KP[%2d]: x=%.4f, y=%.4f, score=%.3f", 
                     i, pose.keypoints[i].x, pose.keypoints[i].y, pose.keypoints[i].score);
        }
        const char* status_str = (posture_status == 1) ? "GOOD" : 
                                 (posture_status == 0) ? "BAD" : "UNDETECTED";
        PR_NOTICE("Neck angle: %.1f deg, Posture: %s", neck_angle, status_str);
        PR_NOTICE("===================================");
    } else {
        // Log if inference fails
        PR_ERR("Inference failed in posture_frame_callback: %d", ret);
    }

    g_last_pose = pose;
    g_neck_angle = neck_angle;
    g_posture_status = posture_status;
}

/* =========================================================
 *                Public API
 * ========================================================= */

/**
 * @brief Get the last detected posture status
 * 
 * @param angle_out Output parameter for neck angle in degrees (can be NULL)
 * @return int 0=BAD, 1=GOOD, 2=UNDETECTED
 */
int posture_get_status(float* angle_out)
{
    if (angle_out != nullptr) {
        *angle_out = g_neck_angle;
    }
    return g_posture_status;
}

/**
 * @brief Get the last pose result
 * 
 * @param pose_out Output parameter for pose result
 * @return OPERATE_RET OPRT_OK if pose is available
 */
OPERATE_RET posture_get_pose(pose_result_t* pose_out)
{
    if (pose_out == nullptr) {
        return OPRT_INVALID_PARM;
    }
    
    memcpy(pose_out, &g_last_pose, sizeof(pose_result_t));
    return OPRT_OK;
}