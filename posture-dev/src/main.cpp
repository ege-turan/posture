/**
 * @file main.cpp
 * @brief Main application entry point using C++ syntax
 *
 * This file contains the main application logic for the Tuya IoT project.
 * It demonstrates C++ implementation with proper linkage for the Tuya SDK.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"
#include "tkl_output.h"
#include "tal_cli.h"

#include <cstdint>

#include <cstdio>
#include <cstring>



extern "C" {
#include "example_display.h"
}

extern "C" {
#include "ui_lvgl.h"
}

extern "C" {
#include "ai_classifier.h"
}

extern "C" {
#include "ble_peripheral_port.h"
}



namespace {

static constexpr std::size_t BLE_INBOX_MAX = 256;

static char g_ble_inbox[BLE_INBOX_MAX];
static volatile bool g_ble_inbox_full = false;

/**
 * Called from BLE stack context. Keep it short: copy bytes, return.
 */
extern "C" void on_ble_rx(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        return;
    }

    if (g_ble_inbox_full) {
        // Drop if previous message not yet consumed.
        return;
    }

    std::size_t n = (len < (BLE_INBOX_MAX - 1)) ? len : (BLE_INBOX_MAX - 1);
    memcpy(g_ble_inbox, data, n);
    g_ble_inbox[n] = '\0';
    g_ble_inbox_full = true;
}


static std::size_t bounded_strlen(const char *s, std::size_t max_n)
{
    std::size_t i = 0;
    for (; i < max_n; ++i) {
        if (s[i] == '\0') break;
    }
    return i;
}

/**
 * Runs in your main loop context. Safe to call UI/LVGL here.
 */
static bool pop_ble_message(char *out, std::size_t out_sz)
{
    if (!g_ble_inbox_full || !out || out_sz == 0) {
        return false;
    }

    std::size_t n = bounded_strlen(g_ble_inbox, BLE_INBOX_MAX);
    if (n >= out_sz) n = out_sz - 1;

    memcpy(out, g_ble_inbox, n);
    out[n] = '\0';

    g_ble_inbox_full = false;
    return true;
}


/**
 * @brief Main application logic
 *
 * Initializes the Tuya logging system and runs the main application loop.
 */
void user_main()
{
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, 
                 reinterpret_cast<TAL_LOG_OUTPUT_CB>(tkl_log_output));
    PR_DEBUG("hello world\r\n");

    static tal_kv_cfg_t kv_cfg{};
    memset(&kv_cfg, 0, sizeof(kv_cfg));

    // Copy into fixed-size arrays safely
    snprintf(kv_cfg.seed, sizeof(kv_cfg.seed), "%s", "vmlkasdh93dlvlcy");
    snprintf(kv_cfg.key,  sizeof(kv_cfg.key),  "%s", "dflfuap134ddlduq");

    tal_kv_init(&kv_cfg);


    tal_sw_timer_init();
    // tal_workq_init(); // erm

    PR_NOTICE("boot: starting BLE");
    //ble_central_start();
    ble_peripheral_port_start();

    // Register RX callback AFTER start (either before or after is fine)
    ble_peripheral_port_set_rx_callback(on_ble_rx);


    tal_system_sleep(2000);
    PR_ERR("boot: starting display init");

    const char* msg1 = "Target: 50% OFF everything today only!!!";
    const char* msg2 = "Calendar: Design Review moved from 3:00pm to 4:00pm on Wed.";
    const char* msg3 = "Meeting moved up: Demo Prep now at 2:00pm (in 10 min)!!";


    //board_register_hardware();
    //display_demo_init();
    ui_lvgl_start();

    char ble_msg[256];

    std::int32_t cnt = 0;
    while (true) {

        //display_demo_step();

        
        if (pop_ble_message(ble_msg, sizeof(ble_msg))) {
            PR_NOTICE("BLE RX: %s", ble_msg);

            // Classify incoming message
            ai_priority_t prio = ai_classify_text(ble_msg);

            // Add to LVGL as a new notification bubble
            ui_add_notification_from_text(ble_msg, prio);
        }


        //PR_DEBUG("AI p1=%d", (int)ai_classify_text(msg1));
        //PR_DEBUG("AI p2=%d", (int)ai_classify_text(msg2));
        //PR_DEBUG("AI p3=%d", (int)ai_classify_text(msg3));

        PR_DEBUG("cnt is %d", cnt++);
        tal_system_sleep(500);
    }
}

} // anonymous namespace

/**
 * @brief Main entry point for Linux systems
 *
 * @param argc Number of command line arguments
 * @param argv Command line arguments array
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
 *
 * Runs the main application logic in a separate thread.
 *
 * @param arg Thread argument (unused)
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
 * It creates and starts a thread to run the main application logic.
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
