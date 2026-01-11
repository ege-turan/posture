/**
 * @file posture_detect.cpp
 * @brief MoveNet-based posture detection with queue-based multi-threaded architecture
 *
 * Implements real-time posture detection using MoveNet SinglePose Lightning INT8.
 * Uses queue-based architecture for asynchronous processing:
 * - Camera captures frames and posts to frame queue
 * - Inference worker processes frames asynchronously (~6s per frame)
 * - Display worker shows popups and notifications
 * - BLE worker receives phone notifications
 *
 * Copyright (c) 2021-2025 Tuya Inc.
 */

#include "inference.h"
#include "camera.h"
#include "queue_types.h"
#include "display_popup.h"
#include "ble_comm.h"

#include "tal_api.h"
#include "tal_queue.h"
#include "tkl_output.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

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
 *                Queue handles
 * ========================================================= */
static QUEUE_HANDLE g_frame_queue = NULL;        // Camera -> Inference worker
static QUEUE_HANDLE g_notify_queue = NULL;       // BLE/Inference -> Display worker

/* =========================================================
 *                Thread handles
 * ========================================================= */
static THREAD_HANDLE g_inference_worker_thread = NULL;
static THREAD_HANDLE g_display_worker_thread = NULL;
static THREAD_HANDLE g_ble_worker_thread = NULL;

/* =========================================================
 *                Pose state
 * ========================================================= */
static pose_result_t g_last_pose = {0};
static float g_neck_angle = 0.0f;
static int g_posture_status = 0;  // 0=BAD, 1=GOOD, 2=UNDETECTED

/* =========================================================
 *                Thread control flags
 * ========================================================= */
static volatile bool g_inference_worker_running = false;
static volatile bool g_display_worker_running = false;
static volatile bool g_ble_worker_running = false;

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
/**
 * @brief Camera frame callback - posts frames to queue for async processing
 * 
 * This runs in the camera callback context. It quickly copies the frame data
 * and posts it to the frame queue, allowing the camera to continue capturing
 * while inference runs asynchronously in a separate thread.
 */
void posture_frame_callback(uint8_t* yuv422_data, int width, int height, void* user_data)
{
    (void)user_data;

    if (g_frame_queue == NULL || yuv422_data == NULL) {
        return;
    }

    // Calculate frame data size (YUV422 = 2 bytes per pixel)
    int frame_size = width * height * 2;

    // Allocate frame queue item
    frame_queue_item_t* item = (frame_queue_item_t*)malloc(sizeof(frame_queue_item_t));
    if (item == NULL) {
        PR_ERR("Failed to allocate frame queue item");
        return;
    }

    // Allocate and copy frame data
    item->frame_data = (uint8_t*)malloc(frame_size);
    if (item->frame_data == NULL) {
        PR_ERR("Failed to allocate frame data");
        free(item);
        return;
    }

    memcpy(item->frame_data, yuv422_data, frame_size);
    item->width = width;
    item->height = height;
    item->timestamp_ms = 0;  // TODO: Get actual timestamp if needed (tal_time_get_posix_ms or similar)

    // Post to queue (non-blocking, timeout 0 = fail immediately if full)
    // Queue stores frame_queue_item_t*, so we pass address of pointer variable
    OPERATE_RET ret = tal_queue_post(g_frame_queue, &item, 0);
    if (ret != OPRT_OK) {
        PR_WARN("Frame queue full, dropping frame");
        free(item->frame_data);
        free(item);
    }
    // If queue is full, frame is dropped (camera continues normally)
}

/* =========================================================
 *                Inference Worker Thread
 * ========================================================= */
/**
 * @brief Inference worker thread task
 * 
 * Processes frames from the frame queue through MoveNet inference.
 * Runs inference asynchronously (~6s per frame) so camera can continue capturing.
 */
static void inference_worker_task(void* args)
{
    (void)args;
    
    PR_NOTICE("Inference worker thread started");

    while (g_inference_worker_running) {
        frame_queue_item_t* item = NULL;
        
        // Block until frame is available (0xFFFFFFFF = wait forever)
        OPERATE_RET ret = tal_queue_fetch(g_frame_queue, &item, 0xFFFFFFFF);
        if (ret != OPRT_OK || item == NULL) {
            continue;
        }

        // Process frame through MoveNet inference
        pose_result_t pose{};
        float neck_angle = 0.0f;
        int posture_status = 2;  // Default to UNDETECTED

        ret = inference_process_frame(item->frame_data, item->width, item->height, &pose);
        if (ret == OPRT_OK) {
            posture_status = posture_good(&pose, neck_angle);
            
            // Update global state (thread-safe access)
            g_last_pose = pose;
            g_neck_angle = neck_angle;
            g_posture_status = posture_status;
            
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

            // Post posture warning to display queue if bad posture detected
            if (posture_status == 0) {  // BAD posture
                notification_queue_item_t notify = {0};
                notify.type = NOTIFY_TYPE_POSTURE_WARNING;
                snprintf(notify.message, sizeof(notify.message), 
                        "Bad posture detected! Neck angle: %.1f deg", neck_angle);
                notify.duration_ms = 5000;  // Show for 5 seconds
                notify.priority = 3;  // High priority

                tal_queue_post(g_notify_queue, &notify, 0);
            }
        } else {
            PR_ERR("Inference failed: %d", ret);
        }

        // Free frame data
        free(item->frame_data);
        free(item);
    }

    PR_NOTICE("Inference worker thread stopped");
}

/* =========================================================
 *                Display Worker Thread
 * ========================================================= */
/**
 * @brief Display worker thread task
 * 
 * Processes notifications from the notification queue and displays popups.
 * Handles both Bluetooth notifications and posture warnings.
 */
static void display_worker_task(void* args)
{
    (void)args;
    
    PR_NOTICE("Display worker thread started");

    while (g_display_worker_running) {
        notification_queue_item_t notify = {0};
        
        // Block until notification is available
        OPERATE_RET ret = tal_queue_fetch(g_notify_queue, &notify, 0xFFFFFFFF);
        if (ret != OPRT_OK) {
            continue;
        }

        // Show popup based on notification type
        switch (notify.type) {
            case NOTIFY_TYPE_PHONE_CALL:
                PR_NOTICE("[DISPLAY] Phone call: %s", notify.message);
                display_popup_show(notify.message, 
                    notify.duration_ms > 0 ? notify.duration_ms : 10000, 
                    notify.priority);
                break;

            case NOTIFY_TYPE_MESSAGE:
                PR_NOTICE("[DISPLAY] Message: %s", notify.message);
                display_popup_show(notify.message, 
                    notify.duration_ms > 0 ? notify.duration_ms : 5000, 
                    notify.priority);
                break;

            case NOTIFY_TYPE_POSTURE_WARNING:
                PR_NOTICE("[DISPLAY] Posture warning: %s", notify.message);
                display_popup_show(notify.message, 
                    notify.duration_ms > 0 ? notify.duration_ms : 5000, 
                    notify.priority);
                break;

            case NOTIFY_TYPE_POSTURE_GOOD:
                PR_NOTICE("[DISPLAY] Posture improved: %s", notify.message);
                display_popup_show(notify.message, 
                    notify.duration_ms > 0 ? notify.duration_ms : 3000, 
                    notify.priority);
                break;

            case NOTIFY_TYPE_SYSTEM_INFO:
                PR_NOTICE("[DISPLAY] System info: %s", notify.message);
                display_popup_show(notify.message, 
                    notify.duration_ms > 0 ? notify.duration_ms : 3000, 
                    notify.priority);
                break;

            default:
                PR_WARN("[DISPLAY] Unknown notification type: %d", notify.type);
                break;
        }
    }

    PR_NOTICE("Display worker thread stopped");
}

/* =========================================================
 *                BLE Worker Thread
 * ========================================================= */
/**
 * @brief BLE notification callback (called from BLE thread context)
 */
static void ble_notification_callback(const char* notification_type, const char* message, void* user_data)
{
    (void)user_data;

    if (g_notify_queue == NULL || notification_type == NULL || message == NULL) {
        return;
    }

    notification_queue_item_t notify = {0};
    
    // Map notification type to queue item type
    if (strcmp(notification_type, "phone_call") == 0) {
        notify.type = NOTIFY_TYPE_PHONE_CALL;
    } else if (strcmp(notification_type, "message") == 0) {
        notify.type = NOTIFY_TYPE_MESSAGE;
    } else {
        notify.type = NOTIFY_TYPE_SYSTEM_INFO;
    }

    strncpy(notify.message, message, sizeof(notify.message) - 1);
    notify.message[sizeof(notify.message) - 1] = '\0';
    notify.duration_ms = 0;  // Use default
    notify.priority = 2;  // Medium priority

    tal_queue_post(g_notify_queue, &notify, 0);
}

/**
 * @brief BLE worker thread task
 * 
 * Monitors BLE for phone notifications and posts them to the notification queue.
 */
static void ble_worker_task(void* args)
{
    (void)args;
    
    PR_NOTICE("BLE worker thread started");

    // Register callback for BLE notifications
    ble_comm_register_callback(ble_notification_callback, NULL);

    // Start BLE communication
    OPERATE_RET ret = ble_comm_start();
    if (ret != OPRT_OK) {
        PR_ERR("Failed to start BLE communication: %d", ret);
        g_ble_worker_running = false;
        return;
    }

    // BLE worker runs continuously, processing notifications via callback
    while (g_ble_worker_running) {
        // BLE notifications are handled asynchronously via callback
        // This thread just needs to stay alive
        tal_system_sleep(1000);
    }

    ble_comm_stop();
    PR_NOTICE("BLE worker thread stopped");
}

/* =========================================================
 *                Queue and Thread Management
 * ========================================================= */

/**
 * @brief Initialize queues and start worker threads
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET posture_detect_queue_init(void)
{
    OPERATE_RET ret = OPRT_OK;

    // Create frame queue (buffer up to 3 frames)
    // Each item is a pointer to frame_queue_item_t
    ret = tal_queue_create_init(&g_frame_queue, sizeof(frame_queue_item_t*), 3);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create frame queue: %d", ret);
        return ret;
    }

    // Create notification queue (buffer up to 10 notifications)
    ret = tal_queue_create_init(&g_notify_queue, sizeof(notification_queue_item_t), 10);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create notification queue: %d", ret);
        tal_queue_free(g_frame_queue);
        g_frame_queue = NULL;
        return ret;
    }

    PR_NOTICE("Queues initialized successfully");
    return OPRT_OK;
}

/**
 * @brief Start all worker threads
 * 
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET posture_detect_threads_start(void)
{
    OPERATE_RET ret = OPRT_OK;
    THREAD_CFG_T thread_cfg = {0};

    // Start inference worker thread
    g_inference_worker_running = true;
    thread_cfg.stackDepth = 8192;  // Need enough stack for inference
    thread_cfg.priority = THREAD_PRIO_2;
    thread_cfg.thrdname = "inference_worker";
    ret = tal_thread_create_and_start(&g_inference_worker_thread, NULL, NULL, 
                                     inference_worker_task, NULL, &thread_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create inference worker thread: %d", ret);
        g_inference_worker_running = false;
        return ret;
    }

    // Start display worker thread
    g_display_worker_running = true;
    thread_cfg.stackDepth = 4096;
    thread_cfg.priority = THREAD_PRIO_3;
    thread_cfg.thrdname = "display_worker";
    ret = tal_thread_create_and_start(&g_display_worker_thread, NULL, NULL, 
                                     display_worker_task, NULL, &thread_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create display worker thread: %d", ret);
        g_display_worker_running = false;
        tal_thread_delete(g_inference_worker_thread);
        g_inference_worker_thread = NULL;
        return ret;
    }

    // Start BLE worker thread
    g_ble_worker_running = true;
    thread_cfg.stackDepth = 4096;
    thread_cfg.priority = THREAD_PRIO_3;
    thread_cfg.thrdname = "ble_worker";
    ret = tal_thread_create_and_start(&g_ble_worker_thread, NULL, NULL, 
                                     ble_worker_task, NULL, &thread_cfg);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to create BLE worker thread: %d", ret);
        g_ble_worker_running = false;
        tal_thread_delete(g_display_worker_thread);
        tal_thread_delete(g_inference_worker_thread);
        g_display_worker_thread = NULL;
        g_inference_worker_thread = NULL;
        return ret;
    }

    PR_NOTICE("All worker threads started successfully");
    return OPRT_OK;
}

/**
 * @brief Stop all worker threads and cleanup queues
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET posture_detect_queue_deinit(void)
{
    // Stop worker threads
    g_inference_worker_running = false;
    g_display_worker_running = false;
    g_ble_worker_running = false;

    // Delete threads and wait for them to exit
    if (g_inference_worker_thread != NULL) {
        tal_thread_delete(g_inference_worker_thread);
        while (g_inference_worker_thread != NULL) {
            tal_system_sleep(100);
        }
    }

    if (g_display_worker_thread != NULL) {
        tal_thread_delete(g_display_worker_thread);
        while (g_display_worker_thread != NULL) {
            tal_system_sleep(100);
        }
    }

    if (g_ble_worker_thread != NULL) {
        tal_thread_delete(g_ble_worker_thread);
        while (g_ble_worker_thread != NULL) {
            tal_system_sleep(100);
        }
    }

    // Free queues
    if (g_frame_queue != NULL) {
        // Drain any remaining items before freeing
        frame_queue_item_t* item = NULL;
        frame_queue_item_t** item_ptr = &item;
        while (tal_queue_fetch(g_frame_queue, item_ptr, 0) == OPRT_OK && item != NULL) {
            if (item->frame_data != NULL) {
                free(item->frame_data);
            }
            free(item);
        }
        tal_queue_free(g_frame_queue);
        g_frame_queue = NULL;
    }

    if (g_notify_queue != NULL) {
        tal_queue_free(g_notify_queue);
        g_notify_queue = NULL;
    }

    PR_NOTICE("Queues and threads deinitialized");
    return OPRT_OK;
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