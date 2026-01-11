/**
 * @file inference.cpp
 * @brief MoveNet inference implementation
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

// Note: TF_LITE_STATIC_MEMORY is defined globally in CMakeLists.txt
// No need to define it here

#include "inference.h"

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"
#include "tkl_memory.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"

#include "model_data.h"  // Include your MoveNet model data

// Helper macro for clamping values
#define CLAMP(x, min, max) (((x) < (min)) ? (min) : (((x) > (max)) ? (max) : (x)))

// MoveNet inference state
static const tflite::Model* sg_model = nullptr;
static tflite::MicroInterpreter* sg_interpreter = nullptr;
static TfLiteTensor* sg_input_tensor = nullptr;
static TfLiteTensor* sg_output_tensor = nullptr;
static uint8_t* sg_tensor_arena = nullptr;
static size_t sg_tensor_arena_size = 0;
static bool sg_initialized = false;

static inference_config_t sg_config = {
    .enable_visualization = false,
    .min_confidence = 0.3f,
};

// Temporary result storage (updated on each inference)
static pose_result_t sg_latest_result = {0};

/**
 * @brief Convert YUV422 (UYVY) to RGB888
 * 
 * @param yuv422_data Input YUV422 data
 * @param width Image width
 * @param height Image height
 * @param rgb_data Output RGB888 buffer (width * height * 3 bytes)
 */
static void yuv422_to_rgb888(uint8_t* yuv422_data, int width, int height, uint8_t* rgb_data)
{
    // TODO: Implement YUV422 to RGB888 conversion
    // This is a simplified conversion - you may want to optimize this
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int yuv_index = (y * width + x) * 2;
            int rgb_index = (y * width + x) * 3;

            // UYVY format: Y is at odd positions (1, 3, 5, ...)
            uint8_t Y = yuv422_data[yuv_index + 1];
            uint8_t U = yuv422_data[yuv_index];
            uint8_t V = yuv422_data[yuv_index + 2];

            // Simple YUV to RGB conversion (for better quality, use proper formulas)
            int R = Y + 1.402f * (V - 128);
            int G = Y - 0.344f * (U - 128) - 0.714f * (V - 128);
            int B = Y + 1.772f * (U - 128);

            rgb_data[rgb_index + 0] = (uint8_t)CLAMP(R, 0, 255);
            rgb_data[rgb_index + 1] = (uint8_t)CLAMP(G, 0, 255);
            rgb_data[rgb_index + 2] = (uint8_t)CLAMP(B, 0, 255);
        }
    }
}

/**
 * @brief Preprocess RGB image for MoveNet input
 * 
 * Resizes input to MoveNet's expected format
 * MoveNet expects: uint8 tensor of shape [192, 192, 3] with values in [0, 255]
 * Channel order: RGB
 * 
 * @param rgb_data Input RGB888 data
 * @param input_width Original width
 * @param input_height Original height
 * @param output_buffer Output preprocessed buffer (MOVENET_INPUT_WIDTH * MOVENET_INPUT_HEIGHT * 3 uint8)
 */
static void preprocess_for_movenet(uint8_t* rgb_data, int input_width, int input_height, 
                                    uint8_t* output_buffer)
{
    // Resize using nearest-neighbor interpolation
    // TODO: Consider implementing bilinear interpolation for better quality
    
    float scale_x = (float)MOVENET_INPUT_WIDTH / input_width;
    float scale_y = (float)MOVENET_INPUT_HEIGHT / input_height;

    for (int y = 0; y < MOVENET_INPUT_HEIGHT; y++) {
        for (int x = 0; x < MOVENET_INPUT_WIDTH; x++) {
            // Nearest neighbor sampling
            int src_x = (int)(x / scale_x);
            int src_y = (int)(y / scale_y);
            
            // Clamp to input bounds
            if (src_x >= input_width) src_x = input_width - 1;
            if (src_y >= input_height) src_y = input_height - 1;
            
            int src_index = (src_y * input_width + src_x) * 3;
            int dst_index = (y * MOVENET_INPUT_WIDTH + x) * 3;

            // Copy RGB values directly (uint8 [0, 255] - no normalization needed)
            // Channel order: RGB
            output_buffer[dst_index + 0] = rgb_data[src_index + 0];  // R
            output_buffer[dst_index + 1] = rgb_data[src_index + 1];  // G
            output_buffer[dst_index + 2] = rgb_data[src_index + 2];  // B
        }
    }
}

/**
 * @brief Direct YUV422 to MoveNet preprocessed format conversion
 * 
 * This function combines YUV422->RGB conversion and resizing in one pass,
 * eliminating the need for an intermediate RGB buffer (saves ~900KB per frame).
 * 
 * @param yuv422_data Input YUV422 data (UYVY format)
 * @param input_width Original image width
 * @param input_height Original image height
 * @param output_buffer Output preprocessed buffer (MOVENET_INPUT_WIDTH * MOVENET_INPUT_HEIGHT * 3 uint8, RGB format)
 */
static void yuv422_to_movenet_preprocessed(uint8_t* yuv422_data, int input_width, int input_height,
                                             uint8_t* output_buffer)
{
    float scale_x = (float)MOVENET_INPUT_WIDTH / input_width;
    float scale_y = (float)MOVENET_INPUT_HEIGHT / input_height;

    for (int y = 0; y < MOVENET_INPUT_HEIGHT; y++) {
        for (int x = 0; x < MOVENET_INPUT_WIDTH; x++) {
            // Nearest neighbor sampling - calculate source pixel position
            int src_x = (int)(x / scale_x);
            int src_y = (int)(y / scale_y);
            
            // Clamp to input bounds
            if (src_x >= input_width) src_x = input_width - 1;
            if (src_y >= input_height) src_y = input_height - 1;
            
            // Calculate YUV422 source index (UYVY format: 2 bytes per pixel)
            int yuv_index = (src_y * input_width + src_x) * 2;
            
            // Extract YUV components from UYVY format
            // UYVY format: [U0, Y0, V0, Y1, U2, Y2, V2, Y3, ...]
            uint8_t Y = yuv422_data[yuv_index + 1];
            uint8_t U = yuv422_data[yuv_index];
            uint8_t V = yuv422_data[yuv_index + 2];

            // Convert YUV to RGB
            int R = Y + 1.402f * (V - 128);
            int G = Y - 0.344f * (U - 128) - 0.714f * (V - 128);
            int B = Y + 1.772f * (U - 128);

            // Clamp and write to output buffer (RGB format)
            int dst_index = (y * MOVENET_INPUT_WIDTH + x) * 3;
            output_buffer[dst_index + 0] = (uint8_t)CLAMP(R, 0, 255);  // R
            output_buffer[dst_index + 1] = (uint8_t)CLAMP(G, 0, 255);  // G
            output_buffer[dst_index + 2] = (uint8_t)CLAMP(B, 0, 255);  // B
        }
    }
}

/**
 * @brief Post-process MoveNet output to extract keypoints
 * 
 * MoveNet output format: float32 tensor of shape [1, 1, 17, 3]
 * The first two channels (indices 0, 1) of the last dimension represent yx coordinates
 * normalized to image frame (range [0.0, 1.0])
 * The third channel (index 2) represents confidence scores [0.0, 1.0]
 * 
 * Keypoint order: [nose, left eye, right eye, left ear, right ear, 
 *                  left shoulder, right shoulder, left elbow, right elbow,
 *                  left wrist, right wrist, left hip, right hip,
 *                  left knee, right knee, left ankle, right ankle]
 * 
 * @param model_output Raw model output tensor (shape [1, 1, 17, 3])
 * @param input_width Original image width (for converting normalized coords to pixels)
 * @param input_height Original image height (for converting normalized coords to pixels)
 * @param result Output pose result
 */
static void postprocess_movenet_output(float* model_output, int input_width, int input_height,
                                       pose_result_t* result)
{
    // MoveNet output tensor shape: [1, 1, 17, 3]
    // To access keypoint i: model_output[i * 3 + 0] = y (normalized 0-1)
    //                      model_output[i * 3 + 1] = x (normalized 0-1)
    //                      model_output[i * 3 + 2] = confidence (0-1)
    
    result->overall_score = 0.0f;
    int valid_keypoints = 0;
    
    for (int i = 0; i < MOVENET_KEYPOINT_COUNT; i++) {
        // Output is stored as [1, 1, 17, 3] but flattened as [17 * 3] = [51] values
        // Layout: [y0, x0, score0, y1, x1, score1, ..., y16, x16, score16]
        int base_offset = i * 3;
        
        float y_norm = model_output[base_offset + 0];  // y coordinate (normalized 0-1)
        float x_norm = model_output[base_offset + 1];  // x coordinate (normalized 0-1)
        float score = model_output[base_offset + 2];   // confidence score (0-1)

        // Store normalized coordinates in result
        // Coordinates are normalized [0.0, 1.0] relative to the 192x192 model input
        // To convert to original image pixel coordinates:
        //   pixel_x = x_norm * input_width
        //   pixel_y = y_norm * input_height
        // Note: These coordinates are relative to the model's 192x192 input frame.
        // If you need coordinates relative to the original image, you'll need to account
        // for any cropping/scaling that was done before resizing.
        result->keypoints[i].y = y_norm;  // Normalized [0.0, 1.0] relative to 192x192
        result->keypoints[i].x = x_norm;  // Normalized [0.0, 1.0] relative to 192x192
        result->keypoints[i].score = score;
        
        if (score > sg_config.min_confidence) {
            result->overall_score += score;
            valid_keypoints++;
        }
    }

    if (valid_keypoints > 0) {
        result->overall_score /= valid_keypoints;
    } else {
        result->overall_score = 0.0f;
    }
}

OPERATE_RET inference_init(const inference_config_t* config)
{
    if (sg_initialized) {
        PR_ERR("Inference already initialized");
        return OPRT_COM_ERROR;
    }

    // Store configuration
    if (config != NULL) {
        memcpy(&sg_config, config, sizeof(inference_config_t));
    }

    // Load model
    sg_model = tflite::GetModel(g_model_data);
    if (sg_model->version() != TFLITE_SCHEMA_VERSION) {
        PR_ERR("Model schema version %d not supported (expected %d)", 
               sg_model->version(), TFLITE_SCHEMA_VERSION);
        return OPRT_INVALID_PARM;
    }

    // Allocate tensor arena from PSRAM heap (8.625MB total, shared with display buffers)
    // Using PSRAM instead of SRAM heap (~245KB free) to support larger models
    // Display buffers need ~1.35MB (3x 450KB), so allocate 1.5MB for tensor arena
    // MoveNet Lightning typically needs 1-2MB, so 1.5MB should be sufficient
    sg_tensor_arena_size = 1536 * 1024;  // 1.5MB
    sg_tensor_arena = (uint8_t*)tkl_system_psram_malloc(sg_tensor_arena_size);
    if (sg_tensor_arena == NULL) {
        PR_ERR("Failed to allocate tensor arena (%zu bytes)", sg_tensor_arena_size);
        return OPRT_MALLOC_FAILED;
    }

    PR_NOTICE("Tensor arena allocated successfully");
    PR_NOTICE("Tensor arena size: %zu bytes", sg_tensor_arena_size);
    PR_NOTICE("Tensor arena pointer: %p", (void*)sg_tensor_arena);

    // Build op resolver - optimized for MoveNet Lightning architecture
    // Based on MoveNet's architecture, we only include operations it actually uses
    // This reduces code size by excluding unused kernels
    static tflite::MicroMutableOpResolver<20> resolver;

    // coming from the get_ops.py script
    resolver.AddAdd();
    resolver.AddArgMax();
    resolver.AddCast();
    resolver.AddConcatenation();
    resolver.AddConv2D();
    // resolver.AddDelegate();
    resolver.AddDepthwiseConv2D();
    resolver.AddDequantize();
    resolver.AddDiv();
    resolver.AddFloorDiv();
    resolver.AddGatherNd();
    resolver.AddLogistic();
    resolver.AddMul();
    resolver.AddPack();
    resolver.AddQuantize();
    resolver.AddReshape();
    resolver.AddResizeBilinear();
    resolver.AddSqrt();
    resolver.AddSub();
    resolver.AddUnpack();

    // Calculate 16-byte aligned pointer
    uintptr_t raw_addr = (uintptr_t)sg_tensor_arena;
    uint8_t* aligned_ptr = (uint8_t*)((raw_addr + 15) & ~15);

    // Calculate how much size we lost to alignment
    size_t alignment_loss = (size_t)(aligned_ptr - sg_tensor_arena);
    size_t effective_arena_size = sg_tensor_arena_size - alignment_loss;

    PR_NOTICE("Alignment Check: Raw=%p, Aligned=%p, Available=%zu", 
            (void*)sg_tensor_arena, (void*)aligned_ptr, effective_arena_size);

    // Use aligned_ptr and effective_arena_size in the constructor below
    sg_interpreter = new tflite::MicroInterpreter(
        sg_model, resolver, aligned_ptr, effective_arena_size);
    
    if (sg_interpreter == nullptr) {
        PR_ERR("Failed to create interpreter");
        tkl_system_psram_free(sg_tensor_arena);
        sg_tensor_arena = NULL;
        return OPRT_MALLOC_FAILED;
    }

    PR_NOTICE("Interpreted successfully");
    
    PR_NOTICE("=== MicroInterpreter Information ===");
    PR_NOTICE("Interpreter pointer: %p", (void*)sg_interpreter);
    PR_NOTICE("Arena size: %zu bytes", sg_tensor_arena_size);
    
    size_t arena_used = sg_interpreter->arena_used_bytes();
    PR_NOTICE("Arena used: %zu bytes (%.1f%% of total)",
              arena_used, (100.0f * arena_used) / sg_tensor_arena_size);
    
    PR_NOTICE("Number of inputs: %zu", sg_interpreter->inputs_size());
    PR_NOTICE("Number of outputs: %zu", sg_interpreter->outputs_size());
    PR_NOTICE("====================================");

    // Allocate memory for tensors
    TfLiteStatus allocate_status = sg_interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        PR_ERR("Failed to allocate tensors");
        tkl_system_psram_free(sg_tensor_arena);
        sg_tensor_arena = NULL;
        return OPRT_COM_ERROR;
    }

    // Get input and output tensors
    sg_input_tensor = sg_interpreter->input(0);
    sg_output_tensor = sg_interpreter->output(0);

    // Verify input tensor shape and type
    if (sg_input_tensor->dims->size != 4 ||
        sg_input_tensor->dims->data[1] != MOVENET_INPUT_HEIGHT ||
        sg_input_tensor->dims->data[2] != MOVENET_INPUT_WIDTH ||
        sg_input_tensor->dims->data[3] != MOVENET_INPUT_CHANNELS) {
        PR_ERR("Invalid input tensor shape. Expected: [1, %d, %d, %d], got: [%d, %d, %d, %d]",
               MOVENET_INPUT_HEIGHT, MOVENET_INPUT_WIDTH, MOVENET_INPUT_CHANNELS,
               sg_input_tensor->dims->data[0],
               sg_input_tensor->dims->data[1],
               sg_input_tensor->dims->data[2],
               sg_input_tensor->dims->data[3]);
        return OPRT_INVALID_PARM;
    }

    // Verify input tensor type is uint8 (MoveNet expects uint8 [0, 255])
    if (sg_input_tensor->type != kTfLiteUInt8) {
        PR_ERR("Invalid input tensor type. Expected uint8, got type: %d", sg_input_tensor->type);
        PR_ERR("Note: MoveNet requires uint8 input tensor with values in [0, 255]");
        return OPRT_INVALID_PARM;
    }

    // Verify output tensor shape [1, 1, 17, 3]
    if (sg_output_tensor->dims->size != 4 ||
        sg_output_tensor->dims->data[2] != MOVENET_KEYPOINT_COUNT ||
        sg_output_tensor->dims->data[3] != 3) {
        PR_ERR("Invalid output tensor shape. Expected: [1, 1, %d, 3], got: [%d, %d, %d, %d]",
               MOVENET_KEYPOINT_COUNT,
               sg_output_tensor->dims->data[0],
               sg_output_tensor->dims->data[1],
               sg_output_tensor->dims->data[2],
               sg_output_tensor->dims->data[3]);
        return OPRT_INVALID_PARM;
    }

    // Verify output tensor type is float32
    if (sg_output_tensor->type != kTfLiteFloat32) {
        PR_ERR("Invalid output tensor type. Expected float32, got type: %d", sg_output_tensor->type);
        return OPRT_INVALID_PARM;
    }

    PR_NOTICE("MoveNet inference initialized successfully");
    PR_NOTICE("Input shape: [%d, %d, %d, %d], type: uint8, range: [0, 255]",
              sg_input_tensor->dims->data[0],
              sg_input_tensor->dims->data[1],
              sg_input_tensor->dims->data[2],
              sg_input_tensor->dims->data[3]);
    PR_NOTICE("Output shape: [%d, %d, %d, %d], type: float32",
              sg_output_tensor->dims->data[0],
              sg_output_tensor->dims->data[1],
              sg_output_tensor->dims->data[2],
              sg_output_tensor->dims->data[3]);

    sg_initialized = true;
    return OPRT_OK;
}

OPERATE_RET inference_process_frame(uint8_t* yuv422_data, int input_width, int input_height,
                                     pose_result_t* result)
{
    if (!sg_initialized || result == NULL || yuv422_data == NULL) {
        return OPRT_INVALID_PARM;
    }

    // Direct conversion: YUV422 -> preprocessed 192x192 RGB (eliminates intermediate buffer)
    // This saves ~900KB of PSRAM per frame (640*480*3 = 921,600 bytes)
    // MoveNet expects uint8 tensor, so we write directly to the tensor's uint8 buffer
    uint8_t* preprocessed = sg_input_tensor->data.uint8;
    yuv422_to_movenet_preprocessed(yuv422_data, input_width, input_height, preprocessed);

    // Step 3: Run inference
    TfLiteStatus invoke_status = sg_interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        PR_ERR("Model inference failed");
        return OPRT_COM_ERROR;
    }

    // Step 4: Post-process output
    float* output_data = sg_output_tensor->data.f;
    postprocess_movenet_output(output_data, input_width, input_height, result);

    // Store latest result
    memcpy(&sg_latest_result, result, sizeof(pose_result_t));

    return OPRT_OK;
}

OPERATE_RET inference_get_latest_result(pose_result_t* result)
{
    if (!sg_initialized || result == NULL) {
        return OPRT_INVALID_PARM;
    }

    memcpy(result, &sg_latest_result, sizeof(pose_result_t));
    return OPRT_OK;
}

OPERATE_RET inference_deinit(void)
{
    if (!sg_initialized) {
        return OPRT_OK;
    }

    // Note: In embedded systems, we don't delete the interpreter to avoid
    // global destructor issues with -fno-exceptions. The interpreter remains
    // allocated but unused. For proper cleanup, use placement new instead of new.
    // For now, just mark as uninitialized - the program runs indefinitely anyway.
    sg_interpreter = nullptr;

    if (sg_tensor_arena != NULL) {
        tkl_system_psram_free(sg_tensor_arena);
        sg_tensor_arena = NULL;
        sg_tensor_arena_size = 0;
    }

    sg_model = nullptr;
    sg_input_tensor = nullptr;
    sg_output_tensor = nullptr;
    sg_initialized = false;

    memset(&sg_latest_result, 0, sizeof(pose_result_t));

    PR_NOTICE("MoveNet inference deinitialized");
    return OPRT_OK;
}
