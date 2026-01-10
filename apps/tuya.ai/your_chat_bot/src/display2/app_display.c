/**
 * @file app_display.c
 * @brief app_display module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "app_display.h"
#include "tal_system.h"
#include "tuya_lvgl.h"

#include "tal_api.h"

#include "ui.h"

/***********************************************************
************************macro define************************
***********************************************************/
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define APP_DISPLAY_MALLOC tal_psram_malloc
#define APP_DISPLAY_FREE   tal_psram_free
#else
#define APP_DISPLAY_MALLOC tal_malloc
#define APP_DISPLAY_FREE   tal_free
#endif

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    TY_DISPLAY_TYPE_E type;
    int               len;
    char             *data;
} DISPLAY_MSG_T;

typedef struct {
    QUEUE_HANDLE  queue_hdl;
    THREAD_HANDLE thrd_hdl;
} TUYA_DISPLAY_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static TUYA_DISPLAY_T sg_app_display = {0};

/***********************************************************
***********************function define**********************
***********************************************************/

static __attribute__((unused)) void __app_display_msg_handle(DISPLAY_MSG_T *msg_data)
{
    if (msg_data == NULL) {
        return;
    }

    TY_DISPLAY_TYPE_E type = msg_data->type;

    switch (type) {
    case TY_DISPLAY_TP_EMOTION: {
        ui_set_emotion(msg_data->data);
    } break;
    case TY_DISPLAY_TP_STATUS: {
        // STANDBY
        // LISTENING
        // SPEAKING
        ui_set_device_status(msg_data->data);
    } break;
    case TY_DISPLAY_TP_USER_MSG: {
        ui_set_user_msg(msg_data->data);
    } break;
    case TY_DISPLAY_TP_ASSISTANT_MSG: {
        ui_set_assistant_msg(msg_data->data);
    } break;
    case TY_DISPLAY_TP_SYSTEM_MSG: {
        ui_set_system_msg(msg_data->data);
    } break;
    default:
        break;
    }

    return;
}

static void __chat_bot_ui_task(void *args)
{
    (void)args;

    DISPLAY_MSG_T msg_data = {0};

    tuya_lvgl_mutex_lock();
    ui_init();
    tuya_lvgl_mutex_unlock();

    tal_system_sleep(50);

    tuya_lvgl_mutex_lock();
    ui_set_system_msg(SYSTEM_MSG_POWER_ON);
    tuya_lvgl_mutex_unlock();
    for (;;) {
        memset(&msg_data, 0, sizeof(DISPLAY_MSG_T));
        tal_queue_fetch(sg_app_display.queue_hdl, &msg_data, QUEUE_WAIT_FOREVER);

        tuya_lvgl_mutex_lock();
        __app_display_msg_handle(&msg_data);
        tuya_lvgl_mutex_unlock();

        if (msg_data.data) {
            APP_DISPLAY_FREE(msg_data.data);
            msg_data.data = NULL;
        }
    }
}
/**
 * @brief Initialize the display system
 *
 * @param None
 * @return OPERATE_RET Initialization result, OPRT_OK indicates success
 */
OPERATE_RET app_display_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    // lvgl initialization
    TUYA_CALL_ERR_RETURN(tuya_lvgl_init());

    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&sg_app_display.queue_hdl, sizeof(DISPLAY_MSG_T), 8));

    THREAD_CFG_T cfg = {
        .thrdname   = "app_ui_msg",
        .priority   = THREAD_PRIO_2,
        .stackDepth = 1024 * 4,
    };
    TUYA_CALL_ERR_RETURN(
        tal_thread_create_and_start(&sg_app_display.thrd_hdl, NULL, NULL, __chat_bot_ui_task, NULL, &cfg));

    PR_DEBUG("app display init success");

    return rt;
}

/**
 * @brief Send display message to the display system
 *
 * @param type Type of the display message
 * @param data Pointer to the message data
 * @param len Length of the message data
 * @return OPERATE_RET Result of sending the message, OPRT_OK indicates success
 */
OPERATE_RET app_display_send_msg(TY_DISPLAY_TYPE_E type, uint8_t *data, int len)
{
    DISPLAY_MSG_T msg_data;

    if (NULL == sg_app_display.queue_hdl) {
        return OPRT_COM_ERROR;
    }

    msg_data.type = type;
    msg_data.len  = len;

    if (len && data != NULL) {
        msg_data.data = APP_DISPLAY_MALLOC(len + 1);
        if (NULL == msg_data.data) {
            return OPRT_MALLOC_FAILED;
        }
        memcpy(msg_data.data, data, len);
        msg_data.data[len] = 0; //"\0"
    } else {
        msg_data.data = NULL;
    }

    tal_queue_post(sg_app_display.queue_hdl, &msg_data, QUEUE_WAIT_FOREVER);

    return OPRT_OK;
}