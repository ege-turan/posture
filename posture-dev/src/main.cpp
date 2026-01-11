/**
 * @file main.cpp
 * @brief Main application entry point - simplified interface for posture detection
 */

 #include "tal_api.h"
 #include "tkl_output.h"
 #include "tal_cli.h"
 
 #include "camera.h"
 #include "inference.h"
 
 #include <cstdint>
 
 extern "C" {
     void* __dso_handle = (void*) &__dso_handle;
 }
 
 namespace {
 
 // Global inference state
 static pose_result_t g_pose_result = {0};
 static bool g_inference_enabled = true;
 
 /**
  * @brief Camera frame callback - processes frame through MoveNet
  */
 void on_camera_frame(uint8_t* yuv422_data, int width, int height, void* user_data)
 {
     (void)user_data; 
 
     if (!g_inference_enabled) {
         return; 
     }
 
     OPERATE_RET ret = inference_process_frame(yuv422_data, width, height, &g_pose_result);
     
     if (ret == OPRT_OK) {
         static int frame_count = 0;
         if (++frame_count % 30 == 0) {
             PR_DEBUG("Pose detected - Overall score: %.2f", g_pose_result.overall_score);
             
             if (g_pose_result.keypoints[0].score > 0.5f) { // Nose
                 PR_DEBUG("  Nose: (%.1f, %.1f) score: %.2f",
                         g_pose_result.keypoints[0].x,
                         g_pose_result.keypoints[0].y,
                         g_pose_result.keypoints[0].score);
             }
         }
     } else {
         PR_ERR("Inference failed: %d", ret);
     }
 }
 
 /**
  * @brief Main application logic
  */
 void user_main()
 {
     OPERATE_RET ret = OPRT_OK;
 
     tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, 
                  reinterpret_cast<TAL_LOG_OUTPUT_CB>(tkl_log_output));
     
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
 
     ret = camera_start(on_camera_frame, nullptr);
     if (ret != OPRT_OK) {
         PR_ERR("Failed to start camera: %d", ret);
         camera_deinit();
         inference_deinit();
         return;
     }
     PR_NOTICE("Camera started - processing frames with MoveNet inference");
 
     while (true) {
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