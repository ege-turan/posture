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

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#include "model_data.h"

#include <cstdint>

namespace {

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

    std::int32_t cnt = 0;
    while (true) {
        PR_DEBUG("cnt is %d", cnt++);
        tal_system_sleep(1000);
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
