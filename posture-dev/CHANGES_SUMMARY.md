# Queue-Based Architecture Implementation - Changes Summary

This document summarizes all changes made to implement the queue-based multi-threaded architecture following the `os_queue` example pattern.

## Overview

The application has been refactored to use a queue-based multi-threaded architecture for asynchronous processing. This allows the camera to continue capturing frames while inference runs asynchronously (~6 seconds per frame), preventing camera stream freezing.

## Architecture Pattern

The implementation follows the `os_queue` example pattern:
- Queue creation using `tal_queue_create_init()`
- Queue posting using `tal_queue_post()`
- Queue fetching using `tal_queue_fetch()`
- Thread creation using `tal_thread_create_and_start()`
- Thread cleanup using `tal_thread_delete()` with proper wait loops
- Queue cleanup using `tal_queue_free()`

## Files Created

### 1. `include/queue_types.h`
**Purpose**: Defines queue data structures for inter-thread communication

**Contents**:
- `frame_queue_item_t`: Structure for frame queue items (camera → inference)
  - `frame_data`: Pointer to YUV422 frame data
  - `width`, `height`: Frame dimensions
  - `timestamp_ms`: Frame timestamp
- `notification_queue_item_t`: Structure for notification queue items (inference/BLE → display)
  - `type`: Notification type enum (phone call, message, posture warning, etc.)
  - `message`: Notification message text (128 chars)
  - `duration_ms`: Display duration
  - `priority`: Priority level

### 2. `include/ble_comm.h` & `src/ble_comm.c`
**Purpose**: BLE communication interface (temporary stub implementation)

**Functions**:
- `ble_comm_init()`: Initialize BLE stack (stub)
- `ble_comm_start()`: Start BLE advertising/listening (stub)
- `ble_comm_stop()`: Stop BLE communication (stub)
- `ble_comm_deinit()`: Deinitialize BLE (stub)
- `ble_comm_register_callback()`: Register notification callback (stub)

**Note**: All functions are stub implementations that log messages but do nothing. TODO markers indicate where actual BLE functionality should be implemented.

### 3. `include/display_popup.h` & `src/display_popup.c`
**Purpose**: Display popup interface (temporary stub implementation)

**Functions**:
- `display_popup_show()`: Show popup message (stub, logs message)
- `display_popup_hide()`: Hide popup (stub, sets flag)
- `display_popup_is_visible()`: Check if popup is visible (stub, returns flag)

**Note**: All functions are stub implementations. TODO markers indicate where actual display functionality should be implemented.

### 4. `QUEUE_ARCHITECTURE.md`
**Purpose**: Comprehensive documentation of the queue-based architecture

**Contents**:
- Architecture diagram
- Queue descriptions (frame queue, notification queue)
- Thread descriptions (inference worker, display worker, BLE worker)
- Queue operations (creation, posting, fetching, cleanup)
- Thread lifecycle
- Data flow diagrams
- Benefits
- Memory management
- API reference
- Example usage

## Files Modified

### 1. `src/posture_detect.cpp`
**Major Changes**:

#### Added Includes
- `#include "queue_types.h"`
- `#include "display_popup.h"`
- `#include "ble_comm.h"`
- `#include "tal_queue.h"`

#### Added Queue Handles
```cpp
static QUEUE_HANDLE g_frame_queue = NULL;        // Camera → Inference worker
static QUEUE_HANDLE g_notify_queue = NULL;       // BLE/Inference → Display worker
```

#### Added Thread Handles
```cpp
static THREAD_HANDLE g_inference_worker_thread = NULL;
static THREAD_HANDLE g_display_worker_thread = NULL;
static THREAD_HANDLE g_ble_worker_thread = NULL;
```

#### Added Thread Control Flags
```cpp
static volatile bool g_inference_worker_running = false;
static volatile bool g_display_worker_running = false;
static volatile bool g_ble_worker_running = false;
```

#### Modified `posture_frame_callback()`
- **Before**: Processed frame synchronously through inference
- **After**: Allocates frame queue item, copies frame data, posts to frame queue (non-blocking)
- **Benefit**: Camera callback returns immediately, allowing camera to continue capturing

#### Added `inference_worker_task()`
- **Purpose**: Worker thread that processes frames from frame queue
- **Behavior**:
  - Blocks on frame queue until frame is available
  - Processes frame through MoveNet inference (~6 seconds)
  - Updates global posture state
  - Posts posture warnings to notification queue if bad posture detected
  - Frees frame data after processing
- **Priority**: `THREAD_PRIO_2` (medium-high)
- **Stack**: 8192 bytes (large stack needed for inference)

#### Added `display_worker_task()`
- **Purpose**: Worker thread that processes notifications from notification queue
- **Behavior**:
  - Blocks on notification queue until notification is available
  - Calls `display_popup_show()` based on notification type
  - Handles different notification types with appropriate durations
- **Priority**: `THREAD_PRIO_3` (medium)
- **Stack**: 4096 bytes

#### Added `ble_worker_task()`
- **Purpose**: Worker thread that monitors BLE for phone notifications
- **Behavior**:
  - Registers callback with BLE communication module
  - Starts BLE communication
  - Processes notifications via callback (`ble_notification_callback`)
  - Posts notifications to notification queue
- **Priority**: `THREAD_PRIO_3` (medium)
- **Stack**: 4096 bytes

#### Added `ble_notification_callback()`
- **Purpose**: Callback function called by BLE worker when notification received
- **Behavior**:
  - Creates notification queue item
  - Maps notification type to queue item type
  - Posts to notification queue

#### Added `posture_detect_queue_init()`
- **Purpose**: Initialize queues
- **Behavior**:
  - Creates frame queue (3 items capacity, stores `frame_queue_item_t*`)
  - Creates notification queue (10 items capacity, stores `notification_queue_item_t`)
  - Returns error if queue creation fails
- **Following `os_queue` pattern**: Uses `tal_queue_create_init()`

#### Added `posture_detect_threads_start()`
- **Purpose**: Start all worker threads
- **Behavior**:
  - Sets running flags to true
  - Creates and starts inference worker thread
  - Creates and starts display worker thread
  - Creates and starts BLE worker thread
  - Returns error if thread creation fails (with cleanup)
- **Following `os_queue` pattern**: Uses `tal_thread_create_and_start()`

#### Added `posture_detect_queue_deinit()`
- **Purpose**: Stop threads and cleanup queues
- **Behavior**:
  - Sets running flags to false
  - Deletes threads using `tal_thread_delete()`
  - Waits for threads to exit (checks thread handle is NULL)
  - Drains remaining queue items (frees frame data)
  - Frees queues using `tal_queue_free()`
- **Following `os_queue` pattern**: Waits for threads to exit before freeing queues

### 2. `include/posture_detect.h`
**Added Function Declarations**:
```c
OPERATE_RET posture_detect_queue_init(void);
OPERATE_RET posture_detect_threads_start(void);
OPERATE_RET posture_detect_queue_deinit(void);
```

### 3. `src/main.cpp`
**Major Changes**:

#### Added Includes
- `#include "ble_comm.h"`
- `#include "display_popup.h"`

#### Modified `user_main()`
**Initialization Order** (following proper dependency order):
1. Initialize inference engine
2. Initialize BLE communication
3. Initialize queues (`posture_detect_queue_init()`)
4. Start worker threads (`posture_detect_threads_start()`)
5. Initialize camera
6. Start camera with frame callback

**Cleanup Order** (reverse of initialization):
1. Stop camera
2. Deinitialize queues (`posture_detect_queue_deinit()`)
3. Deinitialize camera
4. Deinitialize BLE
5. Deinitialize inference

**Main Loop**:
- Periodically checks posture status
- Logs status every 5 seconds
- No blocking operations (camera and inference run asynchronously)

## Queue Implementation Details

### Frame Queue
- **Type**: Stores `frame_queue_item_t*` (pointers)
- **Item Size**: `sizeof(frame_queue_item_t*)` (8 bytes on 64-bit systems)
- **Capacity**: 5 items
- **Posting**: Allocates frame item, copies frame data, posts pointer (non-blocking, timeout 0)
- **Fetching**: Fetches pointer, processes frame, frees frame data
- **Memory Management**: Producer (camera callback) allocates, consumer (inference worker) frees

### Notification Queue
- **Type**: Stores `notification_queue_item_t` (values)
- **Item Size**: `sizeof(notification_queue_item_t)` (~148 bytes)
- **Capacity**: 10 items
- **Posting**: Creates notification item on stack, posts value (non-blocking, timeout 0)
- **Fetching**: Fetches value, processes notification
- **Memory Management**: Queue copies values (no manual memory management needed)

## Thread Implementation Details

### Thread Creation (Following `os_queue` Pattern)
```c
THREAD_CFG_T thread_cfg = {0};
thread_cfg.stackDepth = 8192;
thread_cfg.priority = THREAD_PRIO_2;
thread_cfg.thrdname = "inference_worker";
g_inference_worker_running = true;
tal_thread_create_and_start(&g_inference_worker_thread, NULL, NULL, 
                             inference_worker_task, NULL, &thread_cfg);
```

### Thread Cleanup (Following `os_queue` Pattern)
```c
// Stop thread
g_inference_worker_running = false;

// Delete thread
tal_thread_delete(g_inference_worker_thread);

// Wait for thread to exit (thread sets handle to NULL)
while (g_inference_worker_thread != NULL) {
    tal_system_sleep(100);
}
```

### Queue Cleanup (Following `os_queue` Pattern)
```c
// Drain remaining items before freeing (for frame queue)
frame_queue_item_t* item = NULL;
frame_queue_item_t** item_ptr = &item;
while (tal_queue_fetch(g_frame_queue, item_ptr, 0) == OPRT_OK && item != NULL) {
    if (item->frame_data != NULL) {
        free(item->frame_data);
    }
    free(item);
}

// Free queue
tal_queue_free(g_frame_queue);
g_frame_queue = NULL;
```

## Benefits of Queue-Based Architecture

1. **Non-blocking Camera**: Camera can continue capturing while inference runs (~6s)
2. **Frame Buffering**: Queue buffers up to 5 frames, preventing frame loss
3. **Decoupling**: Camera, inference, display, and BLE are decoupled via queues
4. **Concurrency**: Multiple tasks run concurrently (camera capture, inference, display, BLE)
5. **Priority-based**: Different thread priorities ensure inference gets CPU time
6. **Graceful Degradation**: If frame queue is full, frames are dropped (camera continues)
7. **Scalability**: Easy to add more worker threads or queues

## Memory Usage

- **Frame Queue**: 3 items × 8 bytes (pointers) = 24 bytes
- **Notification Queue**: 10 items × ~148 bytes = ~1480 bytes
- **Frame Data**: 480×480×2 bytes (YUV422) = ~460KB per frame (allocated on heap)
- **Thread Stacks**: 
  - Inference worker: 8192 bytes
  - Display worker: 4096 bytes
  - BLE worker: 4096 bytes
  - Total: ~17KB

## Testing Considerations

1. **Queue Full Handling**: Frame queue should handle full condition gracefully (drops frames)
2. **Thread Cleanup**: Threads should exit cleanly when running flags are set to false
3. **Memory Leaks**: Frame data should be freed after processing (check with memory profiler)
4. **Thread Safety**: Global state (`g_last_pose`, `g_neck_angle`, `g_posture_status`) should be accessed safely
5. **Concurrent Access**: Queue operations are thread-safe (Tuya SDK handles locking)

## Future Enhancements

1. **Actual BLE Implementation**: Implement real BLE functionality in `ble_comm.c`
2. **Actual Display Popup**: Implement real display popup functionality in `display_popup.c`
3. **Frame Rate Control**: Add frame rate limiting to prevent queue overflow
4. **Statistics**: Add queue depth monitoring and statistics
5. **Error Recovery**: Add error recovery mechanisms for thread failures
6. **Configurable Queue Sizes**: Make queue sizes configurable at runtime

## API Usage Example

```c
// Initialize
inference_init(&config);
ble_comm_init();
posture_detect_queue_init();
posture_detect_threads_start();
camera_init();
camera_start(posture_frame_callback, nullptr);

// Main loop
while (true) {
    float angle = 0.0f;
    int status = posture_get_status(&angle);
    // ... use status ...
    tal_system_sleep(5000);
}

// Cleanup
camera_stop();
posture_detect_queue_deinit();
camera_deinit();
ble_comm_deinit();
inference_deinit();
```

## Notes

- All queue operations follow the `os_queue` example pattern
- Thread cleanup follows the `os_queue` example pattern (wait for threads to exit)
- BLE and display popup functions are stub implementations (TODO markers)
- Frame queue uses pointers (large data), notification queue uses values (small data)
- Queue posting uses non-blocking mode (timeout 0) to prevent camera callback blocking
- Queue fetching uses blocking mode (timeout 0xFFFFFFFF) to efficiently wait for data
- Thread priorities ensure inference gets CPU time when needed
