# Queue-Based Architecture Implementation - Changes Summary

## Overview

This document summarizes all changes made to implement the queue-based multi-threaded architecture for the posture detection application. The architecture enables asynchronous processing of camera frames, inference, notifications, and display updates.

## Files Created

### 1. `include/queue_types.h`
**Purpose**: Defines data structures for queue items.

**Contents**:
- `frame_queue_item_t`: Structure for camera frames posted to frame queue
  - `frame_data`: Pointer to YUV422 frame data (dynamically allocated)
  - `width`, `height`: Frame dimensions
  - `timestamp_ms`: Frame timestamp (currently unused, set to 0)
- `notification_queue_item_t`: Structure for notification messages
  - `type`: Notification type enum (PHONE_CALL, MESSAGE, POSTURE_WARNING, etc.)
  - `message`: Text message (128 characters max)
  - `duration_ms`: Display duration
  - `priority`: Priority level

### 2. `include/ble_comm.h` & `src/ble_comm.c`
**Purpose**: Temporary stub implementation for Bluetooth Low Energy communication.

**Functions** (all stubs that do nothing):
- `ble_comm_init()`: Initialize BLE stack
- `ble_comm_start()`: Start BLE advertising/listening
- `ble_comm_stop()`: Stop BLE
- `ble_comm_deinit()`: Cleanup BLE resources
- `ble_comm_register_callback()`: Register callback for received notifications

**TODO**: Implement actual BLE functionality for phone notifications.

### 3. `include/display_popup.h` & `src/display_popup.c`
**Purpose**: Temporary stub implementation for display popup messages.

**Functions** (all stubs that log messages):
- `display_popup_show()`: Show popup message
- `display_popup_hide()`: Hide current popup
- `display_popup_is_visible()`: Check if popup is visible

**TODO**: Implement actual display popup rendering with overlay graphics.

### 4. `QUEUE_ARCHITECTURE.md`
**Purpose**: Comprehensive documentation of the queue-based architecture.

**Contents**:
- Architecture diagrams
- Queue structure descriptions
- Thread priorities and stack sizes
- Data flow examples
- Memory management
- Error handling
- API reference

## Files Modified

### 1. `src/posture_detect.cpp`
**Major Changes**:

1. **Added Queue Management**:
   - Global queue handles: `g_frame_queue`, `g_notify_queue`
   - Global thread handles: `g_inference_worker_thread`, `g_display_worker_thread`, `g_ble_worker_thread`
   - Thread control flags: `g_inference_worker_running`, `g_display_worker_running`, `g_ble_worker_running`

2. **Refactored Camera Callback** (`posture_frame_callback`):
   - **Before**: Processed frame directly through inference (blocking)
   - **After**: Allocates frame data, copies YUV422 data, posts to frame queue (non-blocking)
   - Camera continues capturing at 30 FPS without waiting for inference

3. **Added Inference Worker Thread** (`inference_worker_task`):
   - Blocks on frame queue waiting for frames
   - Processes frames through MoveNet inference (~6 seconds)
   - Updates posture status
   - Posts posture warnings to notification queue when bad posture detected

4. **Added Display Worker Thread** (`display_worker_task`):
   - Blocks on notification queue
   - Processes notifications and shows popups
   - Handles different notification types (phone, message, posture warning, etc.)

5. **Added BLE Worker Thread** (`ble_worker_task`):
   - Registers callback for BLE notifications
   - Posts phone notifications to notification queue
   - Runs continuously in background

6. **Added Queue Management Functions**:
   - `posture_detect_queue_init()`: Creates and initializes queues
   - `posture_detect_threads_start()`: Creates and starts all worker threads
   - `posture_detect_queue_deinit()`: Stops threads and frees queues

### 2. `include/posture_detect.h`
**Changes**:
- Added function declarations:
  - `posture_detect_queue_init()`
  - `posture_detect_threads_start()`
  - `posture_detect_queue_deinit()`

### 3. `src/main.cpp`
**Changes**:

1. **Added Includes**:
   - `ble_comm.h`
   - `display_popup.h`

2. **Modified Initialization Sequence**:
   - Initialize inference engine
   - Initialize BLE communication
   - Initialize queues (`posture_detect_queue_init()`)
   - Start worker threads (`posture_detect_threads_start()`)
   - Initialize camera
   - Start camera with callback

3. **Modified Main Loop**:
   - Changed status check interval from 1 second to 5 seconds
   - Removed direct posture status checking (now handled by inference worker)

4. **Added Cleanup Sequence**:
   - Stop camera
   - Deinitialize queues and threads
   - Deinitialize all components in reverse order

### 4. `CMakeLists.txt`
**Changes**: None required - automatically includes new `.c` files from `src/` directory via `aux_source_directory()`.

## Architecture Flow

### Before (Synchronous)
```
Camera → Inference (~6s blocking) → Update Status → Display
```
**Problem**: Camera blocked during inference, can't capture at full speed.

### After (Asynchronous)
```
Camera → Frame Queue → Inference Worker (async) → Notification Queue → Display Worker
                    ↓
              Update Status

BLE Worker → Notification Queue → Display Worker
```
**Benefit**: Camera continues at 30 FPS, inference runs in background, notifications handled independently.

## Thread Configuration

| Component | Thread | Priority | Stack Size | Purpose |
|-----------|--------|----------|------------|---------|
| Camera | Hardware callback | Highest | N/A | Capture frames, post to queue |
| Inference | `inference_worker` | THREAD_PRIO_2 | 8192 bytes | Process frames asynchronously |
| Display | `display_worker` | THREAD_PRIO_3 | 4096 bytes | Show popups and notifications |
| BLE | `ble_worker` | THREAD_PRIO_3 | 4096 bytes | Handle phone notifications |

## Queue Configuration

| Queue | Buffer Size | Item Type | Producer(s) | Consumer |
|-------|-------------|-----------|-------------|----------|
| Frame Queue | 3 items | `frame_queue_item_t*` | Camera callback | Inference worker |
| Notification Queue | 10 items | `notification_queue_item_t` | Inference worker, BLE worker | Display worker |

## Key Improvements

1. **Non-blocking Camera**: Camera can capture at full 30 FPS without waiting for inference
2. **Asynchronous Inference**: Inference runs in background (~6s per frame) without blocking other operations
3. **Responsive Notifications**: Notifications and popups appear immediately via separate display thread
4. **Decoupled Architecture**: Camera, inference, BLE, and display are independent components
5. **Buffering**: Queues provide buffering for burst traffic and prevent data loss
6. **Thread Safety**: Tuya SDK queues are thread-safe

## Memory Considerations

- **Frame Queue**: Up to 3 frames × ~460KB = ~1.4MB when queue full
- **Notification Queue**: 10 items × 140 bytes = 1.4KB (negligible)
- **Thread Stacks**: ~17KB total (8192 + 4096 + 4096)

## Error Handling

- **Frame Queue Full**: Frame is dropped (freed), camera continues normally
- **Notification Queue Full**: Notification is lost, logged, producer continues
- **Thread Creation Failure**: Error logged, partial functionality available

## Testing Considerations

1. **Frame Processing**: Verify frames are queued and processed asynchronously
2. **Notification Display**: Test popups appear for posture warnings and BLE notifications
3. **Queue Overflow**: Test behavior when queues are full (frames/notifications should be dropped, not crash)
4. **Thread Lifecycle**: Verify threads start and stop cleanly
5. **Memory Leaks**: Monitor frame data allocation/deallocation

## Future Work

1. **Implement BLE Communication**: Replace stub functions with actual BLE stack integration
2. **Implement Display Popup**: Replace stub functions with actual display overlay rendering
3. **Frame Skipping**: Implement logic to process every Nth frame to reduce load
4. **Queue Statistics**: Add monitoring for queue depths and drop rates
5. **Event System**: Optionally add `os_event` for status broadcasts

## Compilation Notes

- All new files are automatically included via CMake's `aux_source_directory()`
- New header files in `include/` are automatically included via `target_include_directories()`
- No changes needed to `CMakeLists.txt`
- Linter errors are expected (linter doesn't have access to Tuya SDK headers)

## Migration Guide

If you have existing code that calls `posture_get_status()` or `posture_get_pose()`:
- **No changes needed** - these functions still work, but now update asynchronously
- Status is updated by inference worker thread after processing each frame
- Status may be slightly stale (up to ~6 seconds) during inference

If you have existing camera callback code:
- **Changed**: Callback now posts to queue instead of processing directly
- Camera callback must allocate frame data and post pointer to queue
- Frame data is freed by inference worker after processing
