/**
 * @file main.cpp
 * @brief Main application entry point - simplified interface for posture detection
 *
 * This file demonstrates a clean, high-level interface for camera-based
 * MoveNet posture detection using the Tuya IoT SDK.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "tkl_output.h"
#include "tal_cli.h"

#include "camera.h"
#include "inference.h"

#include <cstdint>

namespace {

// Global inference state
static pose_result_t g_pose_result = {0};
static bool g_inference_enabled = true;

/**
 * @brief Camera frame callback - processes frame through MoveNet
 *
 * This callback is invoked by the camera module whenever a new frame is available.
 * It runs inference on the CPU and updates the pose result.
 */
void on_camera_frame(uint8_t* yuv422_data, int width, int height, void* user_data)
{
    (void)user_data;  // Unused parameter

    if (!g_inference_enabled) {
        return;  // Inference disabled, just display frames
    }

    // Process frame through MoveNet (runs on CPU)
    OPERATE_RET ret = inference_process_frame(yuv422_data, width, height, &g_pose_result);
    
    if (ret == OPRT_OK) {
        // Log keypoints periodically (adjust frequency as needed)
        static int frame_count = 0;
        if (++frame_count % 30 == 0) {  // Log every 30 frames (~1 second at 30fps)
            PR_DEBUG("Pose detected - Overall score: %.2f", g_pose_result.overall_score);
            
            // Log specific keypoints (e.g., nose, left shoulder, right shoulder)
            if (g_pose_result.keypoints[0].score > 0.5f) {  // Nose
                PR_DEBUG("  Nose: (%.1f, %.1f) score: %.2f",
                        g_pose_result.keypoints[0].x,
                        g_pose_result.keypoints[0].y,
                        g_pose_result.keypoints[0].score);
            }
        }
    } else {
        PR_ERR("Inference failed: %d", ret);
    }
}

/**
 * @brief Main application logic
 *
 * Initializes camera, MoveNet inference, and starts processing frames.
 */
void user_main()
{
    OPERATE_RET ret = OPRT_OK;

    // Initialize logging
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, 
                 reinterpret_cast<TAL_LOG_OUTPUT_CB>(tkl_log_output));
    
    PR_NOTICE("=== Posture Detection Application Starting ===");

    // Step 1: Initialize inference engine (MoveNet)
    inference_config_t inf_config = {
        .enable_visualization = false,  // Set to true to overlay keypoints on display
        .min_confidence = 0.3f,        // Minimum keypoint confidence threshold
    };

    ret = inference_init(&inf_config);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to initialize inference: %d", ret);
        return;
    }
    PR_NOTICE("MoveNet inference initialized");

    // Step 2: Initialize camera and display
    ret = camera_init();
    if (ret != OPRT_OK) {
        PR_ERR("Failed to initialize camera: %d", ret);
        inference_deinit();
        return;
    }
    PR_NOTICE("Camera system initialized");

    // Step 3: Start camera capture with inference callback
    ret = camera_start(on_camera_frame, nullptr);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to start camera: %d", ret);
        camera_deinit();
        inference_deinit();
        return;
    }
    PR_NOTICE("Camera started - processing frames with MoveNet inference");

    // Main loop - camera callbacks handle frame processing
    // You can add additional logic here (e.g., pose classification, logging)
    while (true) {
        tal_system_sleep(1000);

        // Optional: Periodically check pose results or perform classification
        // Example: Detect if person is sitting, standing, etc. based on keypoints
    }

    // Cleanup (typically won't reach here, but good practice)
    camera_stop();
    camera_deinit();
    inference_deinit();
}

} // anonymous namespace

/**
 * @brief Main entry point for Linux systems
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
extern "C" void main(int argc, char *argv[])
{
    (void)argc;  // Suppress unused parameter warning
    (void)argv;  // Suppress unused parameter warning
    user_main();
}
#else

/**
 * @brief Tuya application thread handle
 */
static THREAD_HANDLE ty_app_thread = nullptr;

/**
 * @brief Task thread function
 */
static void tuya_app_thread(void *arg)
{
    (void)arg;  // Suppress unused parameter warning
    
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = nullptr;
}

/**
 * @brief Tuya application main entry point
 *
 * This function is called by the Tuya SDK to start the application.
 */
extern "C" void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param{};
    thrd_param.stackDepth = 4096;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = const_cast<char*>("tuya_app_main");
    
    tal_thread_create_and_start(&ty_app_thread, nullptr, nullptr, 
                                tuya_app_thread, nullptr, &thrd_param);
}
#endif
