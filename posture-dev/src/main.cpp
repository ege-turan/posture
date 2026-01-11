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
#include "ble_comm.h"
#include "display_popup.h"

#include <cstdint>
#include <cstring>

extern "C" {
    void* __dso_handle = (void*) &__dso_handle;
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

     // Initialize BLE communication
     ret = ble_comm_init();
     if (ret != OPRT_OK) {
         PR_ERR("Failed to initialize BLE: %d", ret);
         inference_deinit();
         return;
     }
     PR_NOTICE("BLE communication initialized");

     // Initialize queues and worker threads
     ret = posture_detect_queue_init();
     if (ret != OPRT_OK) {
         PR_ERR("Failed to initialize queues: %d", ret);
         ble_comm_deinit();
         inference_deinit();
         return;
     }
     PR_NOTICE("Queues initialized");

     // Start worker threads (inference, display, BLE)
     ret = posture_detect_threads_start();
     if (ret != OPRT_OK) {
         PR_ERR("Failed to start worker threads: %d", ret);
         posture_detect_queue_deinit();
         ble_comm_deinit();
         inference_deinit();
         return;
     }
     PR_NOTICE("Worker threads started");

     // Initialize camera
     ret = camera_init();
     if (ret != OPRT_OK) {
         PR_ERR("Failed to initialize camera: %d", ret);
         posture_detect_queue_deinit();
         ble_comm_deinit();
         inference_deinit();
         return;
     }
     PR_NOTICE("Camera system initialized");

     // Start camera with frame callback (posts to queue)
     ret = camera_start(posture_frame_callback, nullptr);
     if (ret != OPRT_OK) {
         PR_ERR("Failed to start camera: %d", ret);
         posture_detect_queue_deinit();
         camera_deinit();
         ble_comm_deinit();
         inference_deinit();
         return;
     }
     PR_NOTICE("Camera started - frames will be queued for async processing");

     // Main loop: Monitor status and log periodically
     float neck_angle = 0.0f;
     int posture_status = posture_get_status(&neck_angle);
     
     while (true) {
        // Periodically check posture status
        posture_status = posture_get_status(&neck_angle);

        const char* status_str = (posture_status == 1) ? "GOOD" : 
                                 (posture_status == 0) ? "BAD" : "UNDETECTED";
        PR_NOTICE("Posture: %s (Neck angle: %.1f deg)", status_str, neck_angle);
    
        tal_system_sleep(5000);  // Check every 5 seconds
     }

     // Cleanup (normally never reached in embedded systems)
     camera_stop();
     posture_detect_queue_deinit();
     camera_deinit();
     ble_comm_deinit();
     inference_deinit();
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