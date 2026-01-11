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
file(GLOB_RECURSE TFLM_SRC 
    "${TFLM_ROOT}/tensorflow/lite/micro/*.cc"
    "${TFLM_ROOT}/tensorflow/lite/micro/kernels/*.cc"
    "${TFLM_ROOT}/tensorflow/lite/micro/memory_planner/*.cc"
)

# Filter out test files to save 8MB Flash space
list(FILTER TFLM_SRC EXCLUDE REGEX ".*_test.cc$")
list(FILTER TFLM_SRC EXCLUDE REGEX ".*/test_util.cc$")

# Define TFLM specific includes
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
# Also add nimble/host directory for tuya_ble_mempool.h (needed by ble_gap.h)
# Note: This is a private header directory, but needed for ble_gap.h includes
# Use CMAKE_SOURCE_DIR to get the root source directory (where root CMakeLists.txt is)
set(APP_INC 
    ${APP_PATH}/include
    ${APP_PATH}/models
    ${TFLM_INC}
    ${CMAKE_SOURCE_DIR}/src/tal_bluetooth/nimble/host  # For tuya_ble_mempool.h
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
    )

# C++-specific flags (only applied to .cpp/.cc files, not .c files like camera.c)
target_compile_options(${EXAMPLE_LIB}
    PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>        # Disable RTTI for embedded
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>  # Disable exceptions for embedded
    )