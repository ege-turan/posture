/**
 * @file app_button.c
 * @brief app_button module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "app_button.h"

#include "app_lvgl.h"
#include "app_camera.h"

#include "tal_api.h"
#include "tdl_button_manage.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    APP_DISPLAY_MODE_LVGL   = 0,
    APP_DISPLAY_MODE_CAMERA = 1,
} APP_DISPLAY_MODE_E;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static TDL_BUTTON_HANDLE sg_button_hdl = NULL;

static APP_DISPLAY_MODE_E sg_display_mode       = APP_DISPLAY_MODE_LVGL;
static const char        *sg_display_mode_str[] = {
    [APP_DISPLAY_MODE_LVGL]   = "LVGL",
    [APP_DISPLAY_MODE_CAMERA] = "CAMERA",
};

/***********************************************************
***********************function define**********************
***********************************************************/
static void __app_button_set_display_mode(APP_DISPLAY_MODE_E mode)
{
    OPERATE_RET rt = OPRT_OK;

    sg_display_mode = mode;

    PR_DEBUG("set display mode to: %s", sg_display_mode_str[sg_display_mode]);

    if (APP_DISPLAY_MODE_LVGL == sg_display_mode) {
        TUYA_CALL_ERR_LOG(app_camera_display_stop());
        TUYA_CALL_ERR_LOG(app_lvgl_display_start());
    } else {
        TUYA_CALL_ERR_LOG(app_lvgl_display_stop());
        TUYA_CALL_ERR_LOG(app_camera_display_start());
    }

    return;
}

static void __app_button_function_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E event, void *argc)
{
    (void)name;
    (void)argc;

    switch (event) {
    case TDL_BUTTON_PRESS_SINGLE_CLICK: {
        if (APP_DISPLAY_MODE_LVGL == sg_display_mode) {
            __app_button_set_display_mode(APP_DISPLAY_MODE_CAMERA);
        } else {
            __app_button_set_display_mode(APP_DISPLAY_MODE_LVGL);
        }
    } break;
    default: {
        PR_DEBUG("button event: %d not register", event);
    } break;
    }

    return;
}

void app_button_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    TDL_BUTTON_CFG_T button_cfg;

    memset(&button_cfg, 0, sizeof(TDL_BUTTON_CFG_T));
    button_cfg.long_start_valid_time = 3000;
    button_cfg.long_keep_timer       = 1000;
    button_cfg.button_debounce_time  = 50;

    TUYA_CALL_ERR_LOG(tdl_button_create(BUTTON_NAME, &button_cfg, &sg_button_hdl));

    tdl_button_event_register(sg_button_hdl, TDL_BUTTON_PRESS_SINGLE_CLICK, __app_button_function_cb);

    return;
}

void app_button_deinit(void)
{
    if (NULL == sg_button_hdl) {
        return;
    }

    tdl_button_delete(sg_button_hdl);
    sg_button_hdl = NULL;

    return;
}
