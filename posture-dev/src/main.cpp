/**
 * @file main.cpp
 * @brief MoveNet-based posture detection with on-screen display (Tuya SDK)
 *
 * Implements real-time posture detection using MoveNet SinglePose Lightning INT8.
 * Draws skeleton, keypoints, neck angle, and posture status on display.
 *
 * Copyright (c) 2021-2025 Tuya Inc.
 */

#include "tal_api.h"
#include "tkl_output.h"
#include "board_com_api.h"

#include "tdl_camera_manage.h"
#include "tdl_display_manage.h"
#include "tdl_display_draw.h"

#include <cstdint>
#include <cmath>
#include <cstring>

extern "C" {
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
}

/* =========================================================
 *                MoveNet model binary
 * ========================================================= */
// You must convert movenet_lightning_int8.tflite like this:
// xxd -i movenet_lightning_int8.tflite > movenet_model.cc
extern const unsigned char movenet_lightning_int8_tflite[];
extern const unsigned int movenet_lightning_int8_tflite_len;

/* =========================================================
 *                Constants & configuration
 * ========================================================= */
#define CAM_W 192
#define CAM_H 192

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
 *                Tuya globals
 * ========================================================= */
static TDL_CAMERA_HANDLE_T g_cam = nullptr;
static TDL_DISP_HANDLE_T   g_disp = nullptr;
static TDL_DISP_DEV_INFO_T g_disp_info{};
static TDL_DISP_FRAME_BUFF_T* g_fb = nullptr;

/* =========================================================
 *                MoveNet globals
 * ========================================================= */
static constexpr size_t kArenaSize = 600 * 1024;
static uint8_t g_tensor_arena[kArenaSize];

static const tflite::Model* g_model = nullptr;
static tflite::MicroInterpreter* g_interpreter = nullptr;
static TfLiteTensor* g_input = nullptr;
static TfLiteTensor* g_output = nullptr;

/* =========================================================
 *                Pose structures
 * ========================================================= */
struct Keypoint {
    float y;
    float x;
    float score;
};

struct Pose {
    Keypoint kp[MOVENET_KP_NUM];
};

/* =========================================================
 *                Utility math
 * ========================================================= */
static float deg_atan2(float dy, float dx)
{
    return std::atan2(dy, dx) * 57.29578f;
}

/* =========================================================
 *                MoveNet init
 * ========================================================= */
static bool movenet_init()
{
    g_model = tflite::GetModel(movenet_lightning_int8_tflite);
    if (g_model->version() != TFLITE_SCHEMA_VERSION) {
        PR_ERR("TFLite schema mismatch");
        return false;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter interpreter(
        g_model, resolver, g_tensor_arena, kArenaSize);

    g_interpreter = &interpreter;

    if (g_interpreter->AllocateTensors() != kTfLiteOk) {
        PR_ERR("AllocateTensors failed");
        return false;
    }

    g_input  = g_interpreter->input(0);
    g_output = g_interpreter->output(0);

    PR_NOTICE("MoveNet initialized");
    return true;
}

/* =========================================================
 *                Preprocess (YUV422 -> RGB INT8)
 * ========================================================= */
static void preprocess_yuv422_to_rgb(const uint8_t* yuv, int8_t* out)
{
    for (int i = 0, p = 0; i < CAM_W * CAM_H * 2; i += 4) {
        int y0 = yuv[i + 1];
        int y1 = yuv[i + 3];

        out[p++] = y0 - 128;
        out[p++] = y0 - 128;
        out[p++] = y0 - 128;

        out[p++] = y1 - 128;
        out[p++] = y1 - 128;
        out[p++] = y1 - 128;
    }
}

/* =========================================================
 *                Run inference
 * ========================================================= */
static bool movenet_run(const uint8_t* yuv, Pose& pose)
{
    preprocess_yuv422_to_rgb(yuv, g_input->data.int8);

    if (g_interpreter->Invoke() != kTfLiteOk) {
        PR_ERR("Invoke failed");
        return false;
    }

    const float* out = g_output->data.f;
    for (int i = 0; i < MOVENET_KP_NUM; i++) {
        pose.kp[i].y     = out[i * 3 + 0];
        pose.kp[i].x     = out[i * 3 + 1];
        pose.kp[i].score = out[i * 3 + 2];
    }
    return true;
}

/* =========================================================
 *                Posture detection
 * ========================================================= */
static bool posture_good(const Pose& p, float& angle_out)
{
    const auto& nose = p.kp[0];
    const auto& ls   = p.kp[5];
    const auto& rs   = p.kp[6];

    if (nose.score < SCORE_THRESH ||
        ls.score   < SCORE_THRESH ||
        rs.score   < SCORE_THRESH) {
        angle_out = 0;
        return false;
    }

    float sx = (ls.x + rs.x) * 0.5f;
    float sy = (ls.y + rs.y) * 0.5f;

    angle_out = deg_atan2(nose.y - sy, nose.x - sx);
    return std::fabs(angle_out) < GOOD_NECK_ANGLE_DEG;
}

/* =========================================================
 *                Draw pose
 * ========================================================= */
static void draw_pose(const Pose& p)
{
    int w = g_fb->width;
    int h = g_fb->height;

    for (auto& e : EDGES) {
        const auto& a = p.kp[e[0]];
        const auto& b = p.kp[e[1]];
        if (a.score > SCORE_THRESH && b.score > SCORE_THRESH) {
            tdl_disp_draw_line(
                g_fb,
                int(a.x * w), int(a.y * h),
                int(b.x * w), int(b.y * h),
                1
            );
        }
    }
}

/* =========================================================
 *                Camera callback
 * ========================================================= */
static OPERATE_RET cam_cb(TDL_CAMERA_HANDLE_T, TDL_CAMERA_FRAME_T* frame)
{
    Pose pose{};
    float neck_angle = 0;
    bool good = false;

    if (movenet_run(frame->data, pose)) {
        good = posture_good(pose, neck_angle);
    }

    memset(g_fb->frame, 0xFF, g_fb->len);
    draw_pose(pose);

    char buf[32];
    snprintf(buf, sizeof(buf), "Neck: %.1f deg", neck_angle);
    tdl_disp_draw_string(g_fb, 5, 5, buf, 1);

    tdl_disp_draw_string(
        g_fb, 5, 25,
        good ? "GOOD POSTURE" : "BAD POSTURE",
        1
    );

    tdl_disp_dev_flush(g_disp, g_fb);
    return OPRT_OK;
}

/* =========================================================
 *                Init camera & display
 * ========================================================= */
static void init_hw()
{
    board_register_hardware();

    g_disp = tdl_disp_find_dev(DISPLAY_NAME);
    tdl_disp_dev_open(g_disp);
    tdl_disp_dev_get_info(g_disp, &g_disp_info);

    g_fb = tdl_disp_create_frame_buff(
        DISP_FB_TP_PSRAM,
        (CAM_W + 7) / 8 * CAM_H
    );
    g_fb->fmt = TUYA_PIXEL_FMT_MONOCHROME;
    g_fb->width = CAM_W;
    g_fb->height = CAM_H;

    g_cam = tdl_camera_find_dev(CAMERA_NAME);

    TDL_CAMERA_CFG_T cfg{};
    cfg.width = CAM_W;
    cfg.height = CAM_H;
    cfg.out_fmt = TDL_CAMERA_FMT_YUV422;
    cfg.get_frame_cb = cam_cb;

    tdl_camera_dev_open(g_cam, &cfg);
}

/* =========================================================
 *                User main
 * ========================================================= */
static void user_main()
{
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096,
        reinterpret_cast<TAL_LOG_OUTPUT_CB>(tkl_log_output));

    init_hw();
    movenet_init();

    while (true) {
        tal_system_sleep(1000);
    }
}

/* =========================================================
 *                Tuya entry points
 * ========================================================= */
#if OPERATING_SYSTEM == SYSTEM_LINUX
extern "C" void main(int, char**)
{
    user_main();
}
#else
static THREAD_HANDLE app_thread = nullptr;

static void app_task(void*)
{
    user_main();
}

extern "C" void tuya_app_main()
{
    THREAD_CFG_T cfg{};
    cfg.stackDepth = 8192;
    cfg.priority = THREAD_PRIO_1;
    cfg.thrdname = (char*)"posture";

    tal_thread_create_and_start(
        &app_thread, nullptr, nullptr,
        app_task, nullptr, &cfg
    );
}
#endif
