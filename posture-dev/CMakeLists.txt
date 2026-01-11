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

# Filter out test files to save 8MB Flash space (currently overflowed by 64KB)
list(FILTER TFLM_SRC EXCLUDE REGEX ".*_test.cc$")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/test_util.cc$")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/testing/.*")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/test_helpers.*")

# Define TFLM specific includes
# Note: third_party is at vendor/tflm/third_party, not inside tensorflow
set(TFLM_INC
    "${TFLM_ROOT}"
    "${TFLM_ROOT}/tensorflow"
    "${TFLM_ROOT}/third_party/flatbuffers/include"
    "${TFLM_ROOT}/third_party/gemmlowp"
    "${TFLM_ROOT}/third_party/kissfft"
    "${TFLM_ROOT}/third_party/kissfft/tools"
    "${TFLM_ROOT}/third_party/ruy"
)
# --- TFLM ADDITIONS END ---

# APP_SRC
aux_source_directory(${APP_PATH}/src APP_SRC)

# APP_INC
# We merge the original include with the new TFLM includes
set(APP_INC 
    ${APP_PATH}/include
    ${APP_PATH}/models
    ${TFLM_INC}
)

########################################
# Target Configure
########################################
add_library(${EXAMPLE_LIB})
message(STATUS "EXAMPLE_LIB:${APP_PATH}")

target_sources(${EXAMPLE_LIB}
    PRIVATE
        ${APP_SRC}
        ${TFLM_SRC}    # Added TFLM source files
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
target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        "-Os"  # Optimize for size (important for 8MB Flash)
        "-DTF_LITE_STRIP_ERROR_STRINGS"  # Strip debug strings to save ~100-200KB flash
        "-DTF_LITE_STATIC_MEMORY=1"  # Required for TFLM: uses static memory allocation, skips problematic includes
    )

# C++-specific flags (only applied to .cpp/.cc files, not .c files like camera.c)
target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>        # Disable RTTI for embedded
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>  # Disable exceptions for embedded
        $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit> # Removes need for __dso_handle and saves Flash
    )