/**
 * @file main.cpp
 * @brief Main application entry point - simplified interface for posture detection
 */

 #include "tal_api.h"
 #include "tkl_output.h"
 #include "tal_cli.h"
 
#include "camera.h"
#include "inference.h"
#include "posture_detect.h"

#include <cstdint>
#include <cstring>

extern "C" {
    void* __dso_handle = (void*) &__dso_handle;
}

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


namespace {
 
 /**
  * @brief Main application logic
  */
 void user_main()
 {
     OPERATE_RET ret = OPRT_OK;
 
     ret = tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, 
                  reinterpret_cast<TAL_LOG_OUTPUT_CB>(tkl_log_output));
     if (ret != OPRT_OK) {
         PR_ERR("Failed to initialize log: %d", ret);
         return;
     }
     PR_NOTICE("=== Posture Detection Application Starting ===");
     PR_NOTICE("Queue-based multi-threaded architecture");

    // Initialize inference engine
    inference_config_t inf_config = {
        .enable_visualization = false,
        .min_confidence = 0.3f,
    };

    ret = inference_init(&inf_config);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to initialize inference: %d", ret);
        return;
    }
    PR_NOTICE("MoveNet inference initialized");

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
    // Use posture_ble_rx_callback to integrate with notification queue system
    // Forward declaration (posture_ble_rx_callback is defined in posture_detect.cpp)
    ble_peripheral_port_set_rx_callback(posture_ble_rx_callback);

    tal_system_sleep(2000);
    PR_NOTICE("boot: starting display init");

    ui_lvgl_start();

    // Initialize posture detection queue system
    ret = posture_detect_queue_init();
    if (ret != OPRT_OK) {
        PR_ERR("Failed to initialize posture detection queues: %d", ret);
        return;
    }
    PR_NOTICE("Posture detection queues initialized");

    // Start worker threads (inference, display, BLE)
    ret = posture_detect_threads_start();
    if (ret != OPRT_OK) {
        PR_ERR("Failed to start posture detection threads: %d", ret);
        return;
    }
    PR_NOTICE("Posture detection threads started");

    // Initialize camera
    ret = camera_init();
    if (ret != OPRT_OK) {
        PR_ERR("Failed to initialize camera: %d", ret);
        return;
    }
    PR_NOTICE("Camera initialized");

    // Start camera with posture detection callback
    ret = camera_start(posture_frame_callback, nullptr);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to start camera: %d", ret);
        return;
    }
    PR_NOTICE("Camera started - capturing frames");

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

        tal_system_sleep(5000);
    }
}

 
} // anonymous namespace ends here
 
#if OPERATING_SYSTEM == SYSTEM_LINUX
 extern "C" void main(int argc, char *argv[])
 {
     (void)argc; 
     (void)argv; 
     user_main();
 }
 #else
 
 static THREAD_HANDLE ty_app_thread = nullptr;
 
 static void tuya_app_thread(void *arg)
 {
     (void)arg; 
     user_main();
     tal_thread_delete(ty_app_thread);
     ty_app_thread = nullptr;
 }
 
 /**
  * @brief Tuya application main entry point
  * Now correctly exported as a C symbol
  */
 extern "C" __attribute__((used, visibility("default"))) void tuya_app_main(void)
 {
     THREAD_CFG_T thrd_param;
     memset(&thrd_param, 0, sizeof(THREAD_CFG_T));
     thrd_param.stackDepth = 8192; // INCREASED: MoveNet often needs more stack than 4096
     thrd_param.priority = THREAD_PRIO_1;
     thrd_param.thrdname = (char*)"tuya_app_main";
     
     tal_thread_create_and_start(&ty_app_thread, nullptr, nullptr, 
                                 tuya_app_thread, nullptr, &thrd_param);
 }
 #endif