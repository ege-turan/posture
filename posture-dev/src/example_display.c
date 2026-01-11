/**
 * @file example_display.c
 * @brief example_display module is used to demonstrate the usage of display peripherals.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"

#include "tdl_display_manage.h"
#include "tdl_display_draw.h"
#include "board_com_api.h"

/***********************************************************
************************macro define************************
***********************************************************/

#ifndef DISPLAY_NAME
    #ifdef CONFIG_DISPLAY_NAME
        #define DISPLAY_NAME CONFIG_DISPLAY_NAME
    #else
        #define DISPLAY_NAME "display"
    #endif
#endif


/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/
static TDL_DISP_HANDLE_T      sg_tdl_disp_hdl = NULL;
static TDL_DISP_DEV_INFO_T    sg_display_info;
static TDL_DISP_FRAME_BUFF_T *sg_p_display_fb = NULL;
/***********************************************************
***********************function define**********************
***********************************************************/
static uint32_t __disp_get_random_color(uint32_t range)
{
    return tal_system_get_random(range);
}

void display_demo_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t bytes_per_pixel = 0, pixels_per_byte = 0, bpp = 0;
    uint32_t frame_len = 0;

    // Logging: do this once globally in main.cpp ideally.
    // If you keep it here, guard against double-init.
    // tal_log_init(...);

    //board_register_hardware();

    memset(&sg_display_info, 0, sizeof(sg_display_info));

    PR_ERR("Trying tdl_disp_find_dev(%s)", DISPLAY_NAME);
    sg_tdl_disp_hdl = tdl_disp_find_dev(DISPLAY_NAME);
    if(NULL == sg_tdl_disp_hdl) {
        PR_ERR("display dev %s not found", DISPLAY_NAME);
        return;
    }
    PR_ERR("Display handle found!");

    rt = tdl_disp_dev_get_info(sg_tdl_disp_hdl, &sg_display_info);
    if(rt != OPRT_OK) {
        PR_ERR("get display dev info failed, rt: %d", rt);
        return;
    }

    rt = tdl_disp_dev_open(sg_tdl_disp_hdl);
    if(rt != OPRT_OK) {
        PR_ERR("open display dev failed, rt: %d", rt);
        return;
    }

    tdl_disp_set_brightness(sg_tdl_disp_hdl, 100);

    bpp = tdl_disp_get_fmt_bpp(sg_display_info.fmt);
    if (bpp == 0) {
        PR_ERR("Unsupported pixel format: %d", sg_display_info.fmt);
        return;
    }

    if(bpp < 8) {
        pixels_per_byte = 8 / bpp;
        frame_len = (sg_display_info.width + pixels_per_byte - 1) / pixels_per_byte * sg_display_info.height;
    } else {
        bytes_per_pixel = (bpp + 7) / 8;
        frame_len = sg_display_info.width * sg_display_info.height * bytes_per_pixel;
    }

    sg_p_display_fb = tdl_disp_create_frame_buff(DISP_FB_TP_PSRAM, frame_len);
    if(NULL == sg_p_display_fb) {
        PR_ERR("create display frame buff failed");
        return;
    }

    sg_p_display_fb->x_start = 0;
    sg_p_display_fb->y_start = 0;
    sg_p_display_fb->fmt    = sg_display_info.fmt;
    sg_p_display_fb->width  = sg_display_info.width;
    sg_p_display_fb->height = sg_display_info.height;
}

void display_demo_step(void)
{
    if (!sg_tdl_disp_hdl || !sg_p_display_fb) {
        // Not initialized (or init failed)
        return;
    }

    tdl_disp_draw_fill_full(sg_p_display_fb, __disp_get_random_color(0xFFFFFFFF), sg_display_info.is_swap);
    tdl_disp_dev_flush(sg_tdl_disp_hdl, sg_p_display_fb);
}
