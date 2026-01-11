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
 
     ret = camera_init();
     if (ret != OPRT_OK) {
         PR_ERR("Failed to initialize camera: %d", ret);
         inference_deinit();
         return;
     }
     PR_NOTICE("Camera system initialized");
 
     ret = camera_start(posture_frame_callback, nullptr);
     if (ret != OPRT_OK) {
         PR_ERR("Failed to start camera: %d", ret);
         camera_deinit();
         inference_deinit();
         return;
     }
     PR_NOTICE("Camera started - processing frames with MoveNet inference");
 
     // float neck_angle = 0.0f;
     // bool is_good = posture_get_status(&neck_angle);
     
     while (true) {
        // Periodically check posture status
        /*is_good = posture_get_status(&neck_angle);

        PR_NOTICE("Posture: %s (Neck angle: %.1f deg)", 
                is_good ? "GOOD" : "BAD", neck_angle);*/
    
        tal_system_sleep(1000);
     }
 
     camera_stop();
     camera_deinit();
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