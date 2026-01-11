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
#define SCORE_THRESH  0.3f
#define GOOD_NECK_ANGLE_DEG 20.0f

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
static bool g_posture_good = false;

/* =========================================================
 *                Utility math
 * ========================================================= */
static float deg_atan2(float dy, float dx)
{
    return std::atan2(dy, dx) * 57.29578f;
}

/* =========================================================
 *                Posture detection
 * ========================================================= */
static bool posture_good(const pose_result_t* p, float& angle_out)
{
    if (p == nullptr) {
        angle_out = 0.0f;
        return false;
    }

    const auto& nose = p->keypoints[0];
    const auto& ls   = p->keypoints[5];
    const auto& rs   = p->keypoints[6];

    if (nose.score < SCORE_THRESH ||
        ls.score   < SCORE_THRESH ||
        rs.score   < SCORE_THRESH) {
        angle_out = 0.0f;
        return false;
    }

    float sx = (ls.x + rs.x) * 0.5f;
    float sy = (ls.y + rs.y) * 0.5f;

    angle_out = deg_atan2(nose.y - sy, nose.x - sx);
    return std::fabs(angle_out) < GOOD_NECK_ANGLE_DEG;
}


/* =========================================================
 *                Camera callback
 * ========================================================= */
void posture_frame_callback(uint8_t* yuv422_data, int width, int height, void* user_data)
{
    (void)user_data;

    pose_result_t pose{};
    float neck_angle = 0;
    bool good = false;

    // Process frame through MoveNet inference (using inference.cpp)
    OPERATE_RET ret = inference_process_frame(yuv422_data, width, height, &pose);
    if (ret == OPRT_OK) {
        good = posture_good(&pose, neck_angle);
    } else {
        // Log if inference fails
        PR_ERR("Inference failed in posture_frame_callback: %d", ret);
    }

    g_last_pose = pose;
    g_neck_angle = neck_angle;
    g_posture_good = good;
}

/* =========================================================
 *                Public API
 * ========================================================= */

/**
 * @brief Get the last detected posture status
 * 
 * @param angle_out Output parameter for neck angle in degrees (can be NULL)
 * @return true if posture is good, false otherwise
 */
bool posture_get_status(float* angle_out)
{
    if (angle_out != nullptr) {
        *angle_out = g_neck_angle;
    }
    return g_posture_good;
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