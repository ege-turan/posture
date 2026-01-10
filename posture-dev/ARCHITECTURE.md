# Posture Detection Architecture

## Overview

This project implements a camera-based posture detection system using MoveNet (TensorFlow Lite Micro) with a clean, modular architecture designed for easy integration and maintenance.

## Directory Structure

```
posture-dev/
├── src/
│   ├── main.cpp          # Simplified high-level interface
│   ├── camera.c          # Camera capture and display module
│   └── inference.c       # MoveNet inference engine
├── include/
│   ├── camera.h          # Camera module API
│   └── inference.h       # Inference module API
├── models/
│   └── model_data.h      # MoveNet model (generated)
└── CMakeLists.txt        # Build configuration
```

## Architecture Layers

### 1. **Application Layer** (`main.cpp`)
- **Purpose**: High-level orchestration and simple interface
- **Responsibilities**:
  - Initialize camera and inference modules
  - Register frame callback
  - Handle application-level logic (pose classification, logging)
- **Complexity**: Minimal - just 3 function calls!

```cpp
// Simple interface in main.cpp
inference_init(&config);
camera_init();
camera_start(on_camera_frame, nullptr);
```

### 2. **Camera Module** (`camera.c` / `camera.h`)
- **Purpose**: Handle camera capture, display output, and frame routing
- **Responsibilities**:
  - Initialize camera hardware
  - Initialize display hardware
  - Capture YUV422 frames
  - Convert frames for display (RGB565 or monochrome)
  - **Route frames to inference callback** (key integration point)
- **API**:
  - `camera_init()` - Initialize camera and display
  - `camera_start(callback, user_data)` - Start capture with frame callback
  - `camera_stop()` - Stop capture
  - `camera_deinit()` - Cleanup resources

### 3. **Inference Module** (`inference.c` / `inference.h`)
- **Purpose**: MoveNet model inference and preprocessing
- **Responsibilities**:
  - Load and initialize MoveNet model
  - Convert YUV422 → RGB888
  - Preprocess images (resize, normalize)
  - Run MoveNet inference
  - Post-process outputs (extract keypoints)
- **API**:
  - `inference_init(config)` - Initialize MoveNet model
  - `inference_process_frame(yuv_data, width, height, result)` - Process frame
  - `inference_get_latest_result(result)` - Get last inference result
  - `inference_deinit()` - Cleanup resources

## Data Flow

```
Camera Hardware
    ↓ (YUV422 frames)
Camera Callback (camera.c)
    ↓ (route to inference)
Inference Module (inference.c)
    ├─ YUV422 → RGB888 conversion
    ├─ Resize to 192x192
    ├─ Normalize to [-1, 1]
    ├─ Run MoveNet inference
    └─ Extract 17 keypoints
    ↓ (keypoints + scores)
Application Logic (main.cpp)
    └─ Process/classify pose
    ↓ (display)
Display Output (camera.c)
    ├─ YUV422 → RGB565 (DMA2D accelerated)
    └─ Flush to display
```

## Key Design Principles

### 1. **Separation of Concerns**
- Camera module: Hardware I/O, display
- Inference module: ML processing, image conversion
- Main: High-level logic, coordination

### 2. **Callback-Based Architecture**
- Camera calls user-provided callback for each frame
- Enables clean integration without tight coupling
- Inference runs in callback context (CPU processing)

### 3. **Simple Interface in main.cpp**
- Just 3 initialization calls
- Frame processing happens automatically via callback
- Easy to add application-specific logic

### 4. **CPU Access to Frame Data**
- Despite DMA2D acceleration, CPU always receives frame data via callback
- Inference runs on CPU (no conflict with DMA2D)
- Frame data is in CPU-accessible memory

## Integration Points

### Frame Callback (`on_camera_frame`)
```cpp
void on_camera_frame(uint8_t* yuv422_data, int width, int height, void* user_data)
{
    // This runs on CPU when each frame arrives
    inference_process_frame(yuv422_data, width, height, &pose_result);
}
```

### Camera Module Integration
- Camera callback (`__get_camera_raw_frame_rgb565_cb`) calls user callback before display processing
- Frame data is available in CPU memory
- Display conversion happens after inference (doesn't block CPU access)

## Next Steps / Customization

### 1. **Add Pose Classification**
In `main.cpp`, add logic to classify poses based on keypoints:
```cpp
void classify_pose(pose_result_t* result)
{
    // Example: Detect if person is sitting
    float hip_y = (result->keypoints[11].y + result->keypoints[12].y) / 2.0f;
    float knee_y = (result->keypoints[13].y + result->keypoints[14].y) / 2.0f;
    
    if (hip_y > knee_y - threshold) {
        // Person is sitting
    }
}
```

### 2. **Visualize Keypoints on Display**
Set `enable_visualization = true` in inference config and implement overlay drawing in `camera.c`.

### 3. **Adjust Model Parameters**
Modify `MOVENET_INPUT_WIDTH`, `MOVENET_INPUT_HEIGHT` in `inference.h` to match your model.

### 4. **Optimize Performance**
- Adjust `sg_tensor_arena_size` based on actual memory requirements
- Consider frame skipping (process every N frames)
- Optimize YUV422 → RGB conversion (SIMD instructions)

## Memory Considerations

- **Tensor Arena**: 100KB (adjust based on model size)
- **RGB Buffer**: ~900KB for 640x480x3 (temporary, freed after use)
- **Display Buffers**: Double-buffered (handled by camera module)
- **Model**: Embedded in `model_data.h` (compile-time constant)

## Build Configuration

The CMakeLists.txt automatically includes:
- All source files from `src/` directory
- All header files from `include/` directory
- TFLM sources and dependencies
- Proper C++ linkage for mixed C/C++ code
