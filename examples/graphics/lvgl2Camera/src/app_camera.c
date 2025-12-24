/**
 * @file app_camera.c
 * @brief Camera module for capturing and displaying camera frames
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "app_camera.h"
#include "tal_api.h"
#include "tal_dma2d.h"
#include "tdl_camera_manage.h"
#include "tdl_display_manage.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/***********************************************************
************************macro define************************
***********************************************************/
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define APP_CAMERA_MALLOC tal_psram_malloc
#define APP_CAMERA_FREE   tal_psram_free
#else
#define APP_CAMERA_MALLOC tal_malloc
#define APP_CAMERA_FREE   tal_free
#endif

#define APP_CAMERA_FPS       20
#define APP_CAMERA_WIDTH     480
#define APP_CAMERA_HEIGHT    480
#define DMA2D_TIMEOUT_MS     3000
#define DISPLAY_BUFFER_COUNT 2

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    APP_CAMERA_DISPLAY_STATE_STOP  = 0,
    APP_CAMERA_DISPLAY_STATE_START = 1,
} APP_CAMERA_DISPLAY_STATE_E;

/***********************************************************
 * Static Variables
 ***********************************************************/
static TDL_CAMERA_HANDLE_T        sg_camera_hdl           = NULL;
static TAL_DMA2D_HANDLE_T         sg_dma2d_hdl            = NULL;
static TDL_DISP_HANDLE_T          sg_disp_hdl             = NULL;
static APP_CAMERA_DISPLAY_STATE_E sg_camera_display_state = APP_CAMERA_DISPLAY_STATE_STOP;

static TDL_CAMERA_FRAME_T    *sg_camera_frame_buff                     = NULL;
static TDL_DISP_FRAME_BUFF_T *sg_display_buffers[DISPLAY_BUFFER_COUNT] = {NULL};
static uint8_t                sg_current_convert_idx                   = 0;

/***********************************************************
 * Function Implementations
 ***********************************************************/
/**
 * @brief Convert YUV422 frame to RGB565 using DMA2D
 */
static OPERATE_RET __convert_yuv422_to_rgb565(void *camera_frame, void *display_frame)
{
    if (!sg_dma2d_hdl || !camera_frame || !display_frame) {
        PR_ERR("Invalid parameters");
        return OPRT_INVALID_PARM;
    }

    TKL_DMA2D_FRAME_INFO_T in_frame = {.type      = TUYA_FRAME_FMT_YUV422,
                                       .width     = APP_CAMERA_WIDTH,
                                       .height    = APP_CAMERA_HEIGHT,
                                       .axis      = {0, 0},
                                       .width_cp  = 0,
                                       .height_cp = 0,
                                       .pbuf      = camera_frame};

    TKL_DMA2D_FRAME_INFO_T out_frame = {.type      = TUYA_FRAME_FMT_RGB565,
                                        .width     = APP_CAMERA_WIDTH,
                                        .height    = APP_CAMERA_HEIGHT,
                                        .axis      = {0, 0},
                                        .width_cp  = 0,
                                        .height_cp = 0,
                                        .pbuf      = display_frame};

    OPERATE_RET rt = tal_dma2d_convert(sg_dma2d_hdl, &in_frame, &out_frame);
    if (OPRT_OK != rt) {
        PR_ERR("DMA2D convert failed: %d", rt);
        return rt;
    }

    rt = tal_dma2d_wait_finish(sg_dma2d_hdl, DMA2D_TIMEOUT_MS);
    if (OPRT_OK != rt) {
        PR_ERR("DMA2D wait finish failed: %d", rt);
    }

    return rt;
}

/**
 * @brief Process and display camera frame (runs in work queue)
 * @note Uses ping-pong buffer strategy: one buffer for DMA2D conversion, another for display
 */
static void __send_camera_frame_to_display(void *data)
{
    if (!data || !sg_dma2d_hdl) {
        return;
    }

    if (sg_display_buffers[0] == NULL || sg_display_buffers[1] == NULL) {
        PR_ERR("No available display buffer");
        return;
    }

    TDL_CAMERA_FRAME_T *camera_frame = (TDL_CAMERA_FRAME_T *)data;

    // Find next available buffer for DMA2D conversion
    uint8_t convert_idx = sg_current_convert_idx;

    TDL_DISP_FRAME_BUFF_T *display_frame = sg_display_buffers[convert_idx];

    // Configure display frame
    display_frame->fmt     = TUYA_PIXEL_FMT_RGB565;
    display_frame->width   = APP_CAMERA_WIDTH;
    display_frame->height  = APP_CAMERA_HEIGHT;
    display_frame->free_cb = NULL;

    // Convert YUV422 to RGB565
    OPERATE_RET rt = __convert_yuv422_to_rgb565(camera_frame->data, display_frame->frame);
    if (OPRT_OK != rt) {
        PR_ERR("Convert failed: %d", rt);
        return;
    }

    // Find display device if not cached
    if (!sg_disp_hdl) {
        sg_disp_hdl = tdl_disp_find_dev(DISPLAY_NAME);
    }

    // Flush to display
    if (sg_disp_hdl) {
        rt = tdl_disp_dev_flush(sg_disp_hdl, display_frame);
        if (OPRT_OK != rt) {
            PR_ERR("Display flush failed: %d", rt);
        } else {
            sg_current_convert_idx = (sg_current_convert_idx + 1) % DISPLAY_BUFFER_COUNT;
        }
    } else {
        PR_ERR("Display device not found");
    }
}

/**
 * @brief Camera YUV422 frame callback
 */
static OPERATE_RET __camera_frame_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    if (!frame || sg_camera_display_state != APP_CAMERA_DISPLAY_STATE_START) {
        return OPRT_OK;
    }

    if (!sg_camera_frame_buff) {
        return OPRT_INVALID_PARM;
    }

    // Copy frame metadata
    TDL_CAMERA_FRAME_T *new_frame = sg_camera_frame_buff;
    new_frame->id                 = frame->id;
    new_frame->is_i_frame         = frame->is_i_frame;
    new_frame->is_complete        = frame->is_complete;
    new_frame->fmt                = frame->fmt;
    new_frame->width              = frame->width;
    new_frame->height             = frame->height;
    new_frame->data_len           = frame->data_len;
    new_frame->total_frame_len    = frame->total_frame_len;

    // Copy frame data
    memcpy(new_frame->data, frame->data, frame->data_len);

    // Schedule processing in work queue
    tal_workq_schedule(WORKQ_SYSTEM, __send_camera_frame_to_display, new_frame);

    return OPRT_OK;
}

/**
 * @brief Camera JPEG frame callback (placeholder for future use)
 */
static OPERATE_RET __camera_jpeg_frame_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    return frame ? OPRT_OK : OPRT_INVALID_PARM;
}

/**
 * @brief Initialize camera module
 */
OPERATE_RET app_camera_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    // Allocate camera frame buffer (with 4-byte alignment)
    size_t frame_size    = sizeof(TDL_CAMERA_FRAME_T) + APP_CAMERA_WIDTH * APP_CAMERA_HEIGHT * 2 + 3;
    sg_camera_frame_buff = (TDL_CAMERA_FRAME_T *)APP_CAMERA_MALLOC(frame_size);
    if (!sg_camera_frame_buff) {
        PR_ERR("Failed to allocate camera frame buffer");
        return OPRT_MALLOC_FAILED;
    }

    // Align data pointer to 4 bytes for DMA2D conversion
    uint32_t data_addr         = (uint32_t)sg_camera_frame_buff + sizeof(TDL_CAMERA_FRAME_T);
    sg_camera_frame_buff->data = (uint8_t *)((data_addr + 3) & ~3U);

    // Create display frame buffers (ping-pong buffers)
    size_t display_buff_size = APP_CAMERA_WIDTH * APP_CAMERA_HEIGHT * 2;
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    DISP_FB_RAM_TP_E fb_type = DISP_FB_TP_PSRAM;
#else
    DISP_FB_RAM_TP_E fb_type = DISP_FB_TP_SRAM;
#endif

    for (uint8_t i = 0; i < DISPLAY_BUFFER_COUNT; i++) {
        sg_display_buffers[i] = tdl_disp_create_frame_buff(fb_type, display_buff_size);
        if (!sg_display_buffers[i]) {
            PR_ERR("Failed to create display buffer[%d]", i);
            rt = OPRT_MALLOC_FAILED;
            goto __ERR_EXIT;
        }
    }

    // Initialize DMA2D
    rt = tal_dma2d_init(&sg_dma2d_hdl);
    if (OPRT_OK != rt) {
        PR_ERR("Failed to init DMA2D: %d", rt);
        goto __ERR_EXIT;
    }

    // Find and open camera device
    sg_camera_hdl = tdl_camera_find_dev(CAMERA_NAME);
    if (!sg_camera_hdl) {
        PR_ERR("Camera device not found");
        rt = OPRT_NOT_FOUND;
        goto __ERR_EXIT;
    }

    // Configure camera
    TDL_CAMERA_CFG_T cfg = {.fps                      = APP_CAMERA_FPS,
                            .width                    = APP_CAMERA_WIDTH,
                            .height                   = APP_CAMERA_HEIGHT,
                            .get_frame_cb             = __camera_frame_cb,
                            .get_encoded_frame_cb     = __camera_jpeg_frame_cb,
                            .out_fmt                  = TDL_CAMERA_FMT_JPEG_YUV422_BOTH,
                            .encoded_quality.jpeg_cfg = {.enable = 1, .max_size = 25, .min_size = 10}};

    rt = tdl_camera_dev_open(sg_camera_hdl, &cfg);
    if (OPRT_OK != rt) {
        PR_ERR("Failed to open camera: %d", rt);
        goto __ERR_EXIT;
    }

    return OPRT_OK;

__ERR_EXIT:

    return app_camera_deinit();
}

/**
 * @brief Deinitialize camera module
 */
OPERATE_RET app_camera_deinit(void)
{
    OPERATE_RET rt = OPRT_OK;

    // Close camera device if opened
    if (sg_camera_hdl) {
        rt            = tdl_camera_dev_close(sg_camera_hdl);
        sg_camera_hdl = NULL;
    }

    // Deinitialize DMA2D
    if (sg_dma2d_hdl) {
        tal_dma2d_deinit(sg_dma2d_hdl);
        sg_dma2d_hdl = NULL;
    }

    // Free camera frame buffer
    if (sg_camera_frame_buff) {
        APP_CAMERA_FREE(sg_camera_frame_buff);
        sg_camera_frame_buff = NULL;
    }

    // Free all display frame buffers
    for (uint8_t i = 0; i < DISPLAY_BUFFER_COUNT; i++) {
        if (sg_display_buffers[i]) {
            tdl_disp_free_frame_buff(sg_display_buffers[i]);
            sg_display_buffers[i] = NULL;
        }
    }

    return rt;
}

/**
 * @brief Start camera display
 */
OPERATE_RET app_camera_display_start(void)
{
    if (!sg_camera_hdl || !sg_dma2d_hdl) {
        return OPRT_INVALID_PARM;
    }

    sg_camera_display_state = APP_CAMERA_DISPLAY_STATE_START;
    return OPRT_OK;
}

/**
 * @brief Stop camera display
 */
OPERATE_RET app_camera_display_stop(void)
{
    if (!sg_camera_hdl) {
        return OPRT_INVALID_PARM;
    }

    sg_camera_display_state = APP_CAMERA_DISPLAY_STATE_STOP;
    return OPRT_OK;
}