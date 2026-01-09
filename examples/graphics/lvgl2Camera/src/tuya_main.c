/**
 * @file tuya_main.c
 * @brief tuya_main module is used to
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "app_lvgl.h"
#include "app_camera.h"
#include "app_button.h"

#include "tal_api.h"

#include "tal_log.h"
#include "tal_memory.h"
#include "tkl_output.h"

#include "board_com_api.h"
#include <stdint.h>

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
static THREAD_HANDLE lvgl2Camera_hdl = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/

static void app_lvgl2Camera_thread(void *arg)
{
    (void)arg;

    /*hardware register*/
    board_register_hardware();

    /*init button*/
    app_button_init();
    PR_DEBUG("button init success");

    /*init lvgl*/
    app_lvgl_init();
    PR_DEBUG("lvgl init success");

    /*init camera*/
    app_camera_init();
    PR_DEBUG("camera init success");
    // app_camera_display_start();

    while (1) {
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        int psram_free = tal_psram_get_free_heap_size();
#endif
        int sram_free = tal_system_get_free_heap_size();

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        PR_DEBUG("psram free: %d, sram free: %d", psram_free, sram_free);
#else
        PR_DEBUG("sram free: %d", sram_free);
#endif

        tal_system_sleep(3 * 1000);
    }
}

void user_main(void)
{
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    tal_sw_timer_init();
    tal_workq_init();

    PR_NOTICE("Application information:");
    PR_NOTICE("Project name:        %s", PROJECT_NAME);
    PR_NOTICE("App version:         %s", PROJECT_VERSION);
    PR_NOTICE("Compile time:        %s", __DATE__);
    PR_NOTICE("TuyaOpen version:    %s", OPEN_VERSION);
    PR_NOTICE("TuyaOpen commit-id:  %s", OPEN_COMMIT);
    PR_NOTICE("Platform chip:       %s", PLATFORM_CHIP);
    PR_NOTICE("Platform board:      %s", PLATFORM_BOARD);
    PR_NOTICE("Platform commit-id:  %s", PLATFORM_COMMIT);

    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_2;
    thrd_param.thrdname = "lvgl2Camera task";
    tal_thread_create_and_start(&lvgl2Camera_hdl, NULL, NULL, app_lvgl2Camera_thread, NULL, &thrd_param);
    return;
}

#if OPERATING_SYSTEM == SYSTEM_LINUX

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return void
 */
void main(int argc, char *argv[])
{
    user_main();
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief  task thread
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
static void tuya_app_thread(void *arg)
{
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif