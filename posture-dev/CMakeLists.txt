##
# @file CMakeLists.txt
# @brief 
#/

# Enable C++ language support
enable_language(CXX)

# APP_PATH
set(APP_PATH ${CMAKE_CURRENT_LIST_DIR})

# APP_NAME
get_filename_component(APP_NAME ${APP_PATH} NAME)

# --- TFLM ADDITIONS START ---
# Define the internal vendor path
set(TFLM_ROOT "${APP_PATH}/vendor/tflm")

# Collect all TFLM source files recursively
# Note: The actual path is tensorflow/tensorflow/lite/micro (extra tensorflow level)
file(GLOB_RECURSE TFLM_SRC 
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/*.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/kernels/*.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/memory_planner/*.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/arena_allocator/*.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/tflite_bridge/*.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/core/api/*.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/compiler/mlir/lite/core/api/*.cc"  # ErrorReporter implementation
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/core/c/*.cc"
    # Add kernel utility files (these are required by kernels but not in micro/kernels/)
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/kernels/kernel_util.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/kernels/internal/common.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/kernels/internal/quantization_util.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/kernels/internal/tensor_ctypes.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/kernels/internal/portable_tensor_utils.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/kernels/kernel_util.cc"
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/micro_utils.cc"
    # Schema utilities (needed for GetBuiltinCode)
    "${TFLM_ROOT}/tensorflow/tensorflow/compiler/mlir/lite/schema/schema_utils.cc"
    # Reference implementations (used by kernel implementations, especially for INT8 ops)
    # Note: integer_ops contains only headers, but portable_tensor_utils.cc is in internal/
    "${TFLM_ROOT}/tensorflow/tensorflow/lite/kernels/internal/reference/*.cc"
)

# Add CMSIS-NN optimized kernel sources (from vendor/tflm, same as other TFLM kernels)
# CMSIS-NN kernels provide 10-50x speedup for convolution operations on Cortex-M
# Note: These should be in vendor/tflm/tensorflow/tensorflow/lite/micro/kernels/cmsis_nn/
set(CMSIS_NN_KERNEL_DIR "${TFLM_ROOT}/tensorflow/tensorflow/lite/micro/kernels/cmsis_nn")
file(GLOB CMSIS_NN_KERNEL_SRC "${CMSIS_NN_KERNEL_DIR}/*.cc")
if(CMSIS_NN_KERNEL_SRC)
    list(APPEND TFLM_SRC ${CMSIS_NN_KERNEL_SRC})
    list(LENGTH CMSIS_NN_KERNEL_SRC NUM_KERNEL_FILES)
    message(STATUS "Found ${NUM_KERNEL_FILES} CMSIS-NN kernel files in vendor/tflm (including conv.cc and depthwise_conv.cc)")
else()
    message(WARNING "CMSIS-NN kernel files not found in: ${CMSIS_NN_KERNEL_DIR}")
    message(WARNING "TFLM_ROOT is: ${TFLM_ROOT}")
    message(WARNING "You may need to copy CMSIS-NN kernel files from third_party to vendor/tflm")
endif()

# Filter out test files to save 8MB Flash space (currently overflowed by 64KB)
list(FILTER TFLM_SRC EXCLUDE REGEX ".*_test.cc$")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/test_util.cc$")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/testing/.*")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/test_helpers.*")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/debug_log\\.cc$")  # Exclude reference implementation

# Define TFLM specific includes
# Note: All TFLM files (headers and sources) should be in vendor/tflm
# The structure is vendor/tflm/tensorflow/tensorflow/lite/micro/...
# So include paths should allow "tensorflow/lite/micro/..." to resolve correctly
set(TFLM_INC
    "${TFLM_ROOT}/tensorflow"                  # For "tensorflow/lite/micro/..." includes
    "${TFLM_ROOT}/tensorflow/tensorflow"       # Alternative path
    "${TFLM_ROOT}/third_party/flatbuffers/include"
    "${TFLM_ROOT}/third_party/gemmlowp"
    "${TFLM_ROOT}/third_party/kissfft"
    "${TFLM_ROOT}/third_party/kissfft/tools"
    "${TFLM_ROOT}/third_party/ruy"
)

# Add CMSIS-NN include paths
# CMSIS-NN headers are in posture-dev/cmsis-nn/include (with Internal/ subdirectory)
# Note: 
# - CMSIS-NN kernel wrapper files (.cc) use #include "Include/arm_nnfunctions.h" (need cmsis-nn/)
# - CMSIS-NN library source files (.c) use #include "arm_nnfunctions.h" and #include "Internal/arm_nn_compiler.h"
set(CMSIS_NN_ROOT "${APP_PATH}/cmsis-nn")
set(CMSIS_NN_INC
    "${CMSIS_NN_ROOT}"           # For kernel wrappers: "Include/arm_nnfunctions.h"
    "${CMSIS_NN_ROOT}/include"   # For library sources: "arm_nnfunctions.h" and "Internal/arm_nn_compiler.h"
    "${CMSIS_NN_ROOT}/include/Internal"  # Explicitly include Internal subdirectory
)

# Add CMSIS-5 include paths
# CMSIS-5 Core headers are in posture-dev/cmsis-5/include
set(CMSIS_5_ROOT "${APP_PATH}/cmsis-5")
set(CMSIS_5_INC
    "${CMSIS_5_ROOT}/include"
)

# Add CMSIS-DSP include paths
# CMSIS-DSP headers are in posture-dev/cmsis-dsp/include (includes dsp/ subdirectory)
# Note: CMSIS-DSP headers use #include "dsp/..." so we need include/ in the path
set(CMSIS_DSP_ROOT "${APP_PATH}/cmsis-dsp")
set(CMSIS_DSP_INC
    "${CMSIS_DSP_ROOT}/include"   # For "dsp/..." includes (dsp/ is a subdirectory of include/)
    "${CMSIS_DSP_ROOT}/include/dsp"  # Explicitly include dsp subdirectory
)
# --- TFLM ADDITIONS END ---

# APP_SRC
aux_source_directory(${APP_PATH}/src APP_SRC)

list(REMOVE_ITEM APP_SRC ${APP_PATH}/src/ancs_client.c) # !!! removing for peripheral build


# APP_INC
# We merge the original include with the new TFLM includes
set(APP_INC 
    ${APP_PATH}/include
    ${APP_PATH}/models
    ${TFLM_INC}
    ${CMSIS_NN_INC}  # CMSIS-NN headers
    ${CMSIS_5_INC}   # CMSIS-5 Core headers (required by CMSIS-NN)
    ${CMSIS_DSP_INC} # CMSIS-DSP headers (may be used by some operations)

    ${APP_PATH}/ai/inference # FOR AI MODEL IF WANT IT 
    ${APP_PATH}/ai/generated # FOR AI MODEL IF WANT IT 
    ${TFLM_INC}
    ${CMAKE_SOURCE_DIR}/src/peripherals/display/tdl_display/include
    
    # LVGL include paths (should be automatically added via COMPONENT_PUBINC, but adding explicitly)
    # LVGL v9 paths (check which version is enabled via CONFIG_LVGL_VERSION_9)
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v9
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v9/lvgl
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v9/port
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v9/conf
    # LVGL v8 paths (if using v8 instead)
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v8
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v8/lvgl
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v8/port
    ${CMAKE_SOURCE_DIR}/src/liblvgl/v8/conf
)

# --- DISPLAY (TDL) ADDITIONS START ---
set(TDL_DISPLAY_SRC
    ${CMAKE_SOURCE_DIR}/src/peripherals/display/tdl_display/src/tdl_display_format.c
    ${CMAKE_SOURCE_DIR}/src/peripherals/display/tdl_display/src/tdl_display_manage.c
    ${CMAKE_SOURCE_DIR}/src/peripherals/display/tdl_display/src/tdl_display_draw.c
)
# --- DISPLAY (TDL) ADDITIONS END ---

# --- TAL WORKQ (FORCE LINK) ADDITIONS START --- !!!!!!!
set(TAL_WORKQ_SRC
    ${CMAKE_SOURCE_DIR}/src/tal_system/src/tal_workq_service.c
    ${CMAKE_SOURCE_DIR}/src/tal_system/src/tal_workqueue.c
)
# --- TAL WORKQ (FORCE LINK) ADDITIONS END ---


########################################
# Target Configure
########################################
add_library(${EXAMPLE_LIB})
message(STATUS "EXAMPLE_LIB:${APP_PATH}")

# Collect CMSIS-NN source files
# Since all CMSIS-NN kernel wrapper files (.cc) are compiled, we need all corresponding source directories
# ConvolutionFunctions: conv and depthwise_conv operations
# NNSupportFunctions: helper functions used by convolution operations
# TransposeFunctions: transpose operations used by depthwise_conv_wrapper
# BasicMathFunctions: elementwise operations (mul, add) used by mul.cc, add.cc, maximum_minimum.cc
# FullyConnectedFunctions: used by fully_connected.cc
# SoftmaxFunctions: used by softmax.cc
# PadFunctions: used by pad.cc
# SVDFunctions: used by svdf.cc
# PoolingFunctions: used by pooling.cc
# ActivationFunctions: may be used by various kernels
file(GLOB_RECURSE CMSIS_NN_SRC
    "${CMSIS_NN_ROOT}/src/ConvolutionFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/NNSupportFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/TransposeFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/BasicMathFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/FullyConnectedFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/SoftmaxFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/PadFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/SVDFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/PoolingFunctions/*.c"
    "${CMSIS_NN_ROOT}/src/ActivationFunctions/*.c"
)

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRC}
        ${TFLM_SRC}    # Added TFLM source files
        ${CMSIS_NN_SRC}  # Added CMSIS-NN library source files
        ${TAL_WORKQ_SRC} # !!!!!!!!!!
        ${TDL_DISPLAY_SRC}   # Added TDL display sources
        ${APP_PATH}/ai/inference/ai_classifier.c # THIS IS FOR AI MODEL IF WANT IT !!!!
    )

target_include_directories(${EXAMPLE_LIB}
    PRIVATE             # Use PRIVATE - SDK headers are added by root CMakeLists.txt
        ${APP_INC}
    )

# Set C++ standard for all C++ sources (for TFLM and inference.cpp)
set_target_properties(${EXAMPLE_LIB} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Common optimization flags for all sources (C and C++)

# Note: FPU flags enable hardware floating point acceleration on Cortex-M33F
# TARGET_ARCH=cortex-m33 equivalent: -mcpu=cortex-m33
target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-O3"  # Optimize for speed
        "-DTF_LITE_STATIC_MEMORY=1"  # Required for TFLM: uses static memory allocation, skips problematic includes
        "-DCMSIS_NN"  # Enable CMSIS-NN optimized kernels for Cortex-M (10-50x speedup for conv operations)
        "-mcpu=cortex-m33"  # Target ARM Cortex-M33 architecture (required for CMSIS-NN optimizations)
        "-mfloat-abi=hard"  # Use hardware FPU (Cortex-M33F has FPv5-SP-D16)
        "-mfpu=fpv5-sp-d16"  # FPv5 Single-Precision FPU with 16 double-word registers (Cortex-M33)
    )

# C++-specific flags (only applied to .cpp/.cc files, not .c files like camera.c)
target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>        # Disable RTTI for embedded
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>  # Disable exceptions for embedded
        $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit> # Removes need for __dso_handle and saves Flash
    )