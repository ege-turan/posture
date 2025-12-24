/**
 * @file app_lvgl.c
 * @brief app_lvgl module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "app_lvgl.h"

#include "tal_api.h"

#include "lvgl.h"
#include "lv_vendor.h"
#include "lv_port_disp.h"
#include "tal_sw_timer.h"
#include "tal_thread.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static THREAD_HANDLE sg_app_lvgl_thread_handle = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/
static void __app_lvgl_thread_cb(void *arg)
{
    (void)arg;

    uint32_t timer_count = 0;

    lv_vendor_init(DISPLAY_NAME);

    lv_vendor_start(THREAD_PRIO_0, 1024 * 8);

    // lock display, because this task is not lvgl task
    lv_vendor_disp_lock();
    lv_obj_t *screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text_fmt(label, "Hello World! %u", timer_count);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_vendor_disp_unlock();

    while (1) {
        timer_count++;
        lv_vendor_disp_lock();
        lv_label_set_text_fmt(label, "Hello World! %u", timer_count);
        lv_vendor_disp_unlock();

        tal_system_sleep(1000);
    }

    return;
}

void app_lvgl_init(void)
{
    THREAD_CFG_T app_lvgl_thread_cfg = {0};
    app_lvgl_thread_cfg.stackDepth   = 1024 * 4;
    app_lvgl_thread_cfg.priority     = THREAD_PRIO_0;
    app_lvgl_thread_cfg.thrdname     = "app ui";

    tal_thread_create_and_start(&sg_app_lvgl_thread_handle, NULL, NULL, __app_lvgl_thread_cb, NULL,
                                &app_lvgl_thread_cfg);

    return;
}

void app_lvgl_deinit(void)
{
    lv_vendor_stop();

    return;
}

OPERATE_RET app_lvgl_display_start(void)
{
    OPERATE_RET rt = OPRT_OK;

    disp_enable_update();

    return rt;
}

OPERATE_RET app_lvgl_display_stop(void)
{
    OPERATE_RET rt = OPRT_OK;

    disp_disable_update();

    return rt;
}