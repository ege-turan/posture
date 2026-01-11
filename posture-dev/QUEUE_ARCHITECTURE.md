# Queue-Based Multi-Threaded Architecture

## Overview

This document describes the queue-based multi-threaded architecture implemented for the posture detection application. The architecture enables asynchronous processing of camera frames, inference, notifications, and display updates.

## Architecture Diagram

```
┌──────────────────────┐
│   Camera Thread      │  (Camera hardware callback - Tuya SDK)
│   (Continuous)       │
│                      │
│  Captures frames at  │
│  30 FPS, posts to    │
│  frame queue         │
└──────────┬───────────┘
           │
           │ Posts frame_queue_item_t*
           ▼
┌──────────────────────┐
│   Frame Queue        │  (QUEUE_HANDLE g_frame_queue)
│                      │
│  - Buffer: 3 frames  │
│  - Item type:        │
│    frame_queue_item_t*│
│  - Thread-safe       │
│  - Blocking receive  │
└──────────┬───────────┘
           │
           │ Fetched by inference worker
           ▼
┌──────────────────────┐
│ Inference Worker     │  (THREAD_HANDLE g_inference_worker_thread)
│ Thread               │
│                      │
│  - Processes frames  │
│    asynchronously    │
│  - ~6 seconds per    │
│    frame (MoveNet)   │
│  - Updates posture   │
│    status            │
│  - Posts warnings to │
│    notification queue│
└──────────┬───────────┘
           │
           │ Posts notification_queue_item_t
           │ (when bad posture detected)
           ▼
┌──────────────────────┐
│  Notification Queue  │  (QUEUE_HANDLE g_notify_queue)
│                      │
│  - Buffer: 10 items  │
│  - Item type:        │
│    notification_queue_item_t│
│  - Sources:          │
│    * Inference worker│
│    * BLE worker      │
│  - Thread-safe       │
│  - Blocking receive  │
└──────────┬───────────┘
           │
           │ Fetched by display worker
           ▼
┌──────────────────────┐
│  Display Worker      │  (THREAD_HANDLE g_display_worker_thread)
│  Thread              │
│                      │
│  - Shows popups      │
│  - Handles           │
│    notifications     │
│  - Displays warnings │
│  - Camera stream     │
│    continues in      │
│    background        │
└──────────────────────┘

┌──────────────────────┐
│  BLE Worker Thread   │  (THREAD_HANDLE g_ble_worker_thread)
│                      │
│  - Receives phone    │
│    notifications     │
│  - Posts to          │
│    notification queue│
└──────────┬───────────┘
           │
           │ Posts notification_queue_item_t
           └───────────────────┐
                               │
                               ▼
                    ┌──────────────────────┐
                    │  Notification Queue  │
                    └──────────────────────┘
```

## Queue Structures

### Frame Queue (`g_frame_queue`)

**Purpose**: Buffers camera frames for asynchronous inference processing.

**Item Type**: `frame_queue_item_t*` (pointer to dynamically allocated structure)

**Buffer Size**: 3 frames

**Structure**:
```c
typedef struct {
    uint8_t* frame_data;      // YUV422 frame data (allocated, freed by consumer)
    int width;                // Frame width in pixels
    int height;               // Frame height in pixels
    uint64_t timestamp_ms;    // Frame timestamp (milliseconds)
} frame_queue_item_t;
```

**Producer**: Camera callback (`posture_frame_callback`)
- Allocates frame data and item structure
- Copies YUV422 data
- Posts to queue (non-blocking, timeout 0)
- Drops frame if queue is full (prevents blocking camera)

**Consumer**: Inference worker thread (`inference_worker_task`)
- Blocks waiting for frames (`tal_queue_fetch` with 0xFFFFFFFF timeout)
- Processes frame through MoveNet inference
- Frees frame data and item structure after processing

**Flow**:
1. Camera captures frame at 30 FPS
2. Callback quickly copies data and posts to queue
3. Inference worker processes frames sequentially (~6s each)
4. If queue fills up, oldest frames are overwritten (ring buffer behavior) or new frames are dropped (depending on queue implementation)

### Notification Queue (`g_notify_queue`)

**Purpose**: Buffers notification messages for display.

**Item Type**: `notification_queue_item_t` (structure, not pointer)

**Buffer Size**: 10 notifications

**Structure**:
```c
typedef struct {
    enum {
        NOTIFY_TYPE_PHONE_CALL,        // Phone call notification from Bluetooth
        NOTIFY_TYPE_MESSAGE,           // Text message notification from Bluetooth
        NOTIFY_TYPE_POSTURE_WARNING,   // Bad posture warning
        NOTIFY_TYPE_POSTURE_GOOD,      // Posture improved notification
        NOTIFY_TYPE_SYSTEM_INFO        // System information message
    } type;
    char message[128];                 // Notification message text
    uint32_t duration_ms;              // Display duration (0 = default)
    int priority;                      // Priority (higher = more urgent)
} notification_queue_item_t;
```

**Producers**:
1. **Inference Worker**: Posts `NOTIFY_TYPE_POSTURE_WARNING` when bad posture detected
2. **BLE Worker**: Posts `NOTIFY_TYPE_PHONE_CALL` or `NOTIFY_TYPE_MESSAGE` when phone notifications received

**Consumer**: Display worker thread (`display_worker_task`)
- Blocks waiting for notifications
- Shows popup based on notification type
- Handles priority (higher priority popups can override lower priority ones)

**Flow**:
1. Producer creates notification structure and posts to queue
2. Display worker fetches notification and shows popup
3. Popup displays for specified duration
4. Multiple notifications can queue up if display is busy

## Thread Priorities

| Thread | Priority | Reason |
|--------|----------|--------|
| Inference Worker | `THREAD_PRIO_2` | Medium priority - processes frames asynchronously |
| Display Worker | `THREAD_PRIO_3` | Lower priority - handles UI updates |
| BLE Worker | `THREAD_PRIO_3` | Lower priority - handles background notifications |

Camera callback runs in camera hardware interrupt context (highest priority, handled by Tuya SDK).

## Thread Stack Sizes

| Thread | Stack Size | Reason |
|--------|------------|--------|
| Inference Worker | 8192 bytes | Needs large stack for MoveNet inference operations |
| Display Worker | 4096 bytes | Moderate stack for display operations |
| BLE Worker | 4096 bytes | Moderate stack for BLE operations |

## Queue Operations

### Creating Queues

```c
// Frame queue: stores pointers to frame_queue_item_t, buffer size 3
tal_queue_create_init(&g_frame_queue, sizeof(frame_queue_item_t*), 3);

// Notification queue: stores notification_queue_item_t structures, buffer size 10
tal_queue_create_init(&g_notify_queue, sizeof(notification_queue_item_t), 10);
```

### Posting to Queue

```c
// Post frame (non-blocking, drop if full)
tal_queue_post(g_frame_queue, &item_pointer, 0);

// Post notification (non-blocking)
tal_queue_post(g_notify_queue, &notification, 0);
```

**Note**: Timeout 0 means post fails immediately if queue is full. This prevents blocking the producer thread.

### Fetching from Queue

```c
// Fetch frame (blocking, wait forever)
frame_queue_item_t* item = NULL;
tal_queue_fetch(g_frame_queue, &item, 0xFFFFFFFF);

// Fetch notification (blocking, wait forever)
notification_queue_item_t notify;
tal_queue_fetch(g_notify_queue, &notify, 0xFFFFFFFF);
```

**Note**: Timeout 0xFFFFFFFF means wait indefinitely until an item is available.

## Data Flow Examples

### Example 1: Camera Frame Processing

1. Camera captures frame (480x480 YUV422, ~460KB)
2. `posture_frame_callback()` called by camera hardware
3. Callback allocates `frame_queue_item_t` and frame data buffer
4. Copies YUV422 data to allocated buffer
5. Posts pointer to queue (non-blocking)
6. **Camera continues capturing next frame immediately** (no blocking)
7. Inference worker thread fetches frame from queue
8. Runs MoveNet inference (~6 seconds)
9. Updates posture status
10. If bad posture, posts warning to notification queue
11. Frees frame data and item structure

**Key Benefit**: Camera can continue at 30 FPS while inference runs asynchronously.

### Example 2: Phone Notification Display

1. BLE receives phone call notification from paired device
2. `ble_notification_callback()` called
3. Creates `notification_queue_item_t` with type `NOTIFY_TYPE_PHONE_CALL`
4. Posts to notification queue
5. Display worker thread fetches notification
6. Calls `display_popup_show()` to display popup
7. Popup shown for 10 seconds (default for phone calls)
8. Display worker continues to next notification in queue

### Example 3: Bad Posture Warning

1. Inference worker detects bad posture (status = 0)
2. Creates notification with type `NOTIFY_TYPE_POSTURE_WARNING`
3. Sets message: "Bad posture detected! Neck angle: X.X deg"
4. Sets duration: 5000ms (5 seconds)
5. Sets priority: 3 (high priority)
6. Posts to notification queue
7. Display worker shows popup immediately
8. User sees warning on display

## Memory Management

### Frame Queue Items

- **Allocation**: Done in camera callback (`posture_frame_callback`)
- **Deallocation**: Done in inference worker after processing
- **Risk**: If inference is slow and queue fills up, frames may be dropped, preventing memory leaks

### Notification Queue Items

- **Allocation**: Stack-allocated structures (no dynamic allocation)
- **Copying**: Entire structure is copied into queue (value semantics)
- **Risk**: Minimal - fixed-size structures, no pointers

## Error Handling

### Queue Full (Frame Queue)

If frame queue is full:
- `tal_queue_post()` returns error
- Frame is dropped (freed)
- Camera continues normally
- Logs warning: "Frame queue full, dropping frame"

**Why Drop Frames?**
- Prevents blocking camera callback
- Prevents memory buildup
- Inference is already processing older frames

### Queue Full (Notification Queue)

If notification queue is full:
- `tal_queue_post()` returns error
- Notification is lost (logged)
- Producer continues normally

**Mitigation**: 10-item buffer is usually sufficient for burst notifications.

### Thread Errors

If thread creation fails:
- Error logged
- System continues (other threads may still work)
- Partial functionality available

## Thread Lifecycle

### Initialization Order

1. Initialize inference engine
2. Initialize BLE communication
3. Initialize queues (`posture_detect_queue_init()`)
4. Start worker threads (`posture_detect_threads_start()`)
5. Initialize camera
6. Start camera with callback

### Cleanup Order

1. Stop camera
2. Deinitialize queues (`posture_detect_queue_deinit()`)
   - Stops worker threads
   - Waits for threads to exit
   - Drains remaining queue items
   - Frees queue resources
3. Deinitialize camera
4. Deinitialize BLE
5. Deinitialize inference

## Benefits of This Architecture

1. **Non-blocking Camera**: Camera can capture at full speed (30 FPS) regardless of inference speed
2. **Asynchronous Processing**: Inference runs in background (~6s) without blocking other operations
3. **Responsive UI**: Notifications and popups appear immediately via separate display thread
4. **Decoupled Components**: Camera, inference, BLE, and display are independent
5. **Buffering**: Queues provide buffering for burst traffic
6. **Thread Safety**: Tuya SDK queues are thread-safe
7. **Scalability**: Easy to add more producers/consumers

## Performance Considerations

### Frame Processing Rate

- **Camera Capture**: 30 FPS (33ms per frame)
- **Inference Speed**: ~6 seconds per frame
- **Effective Processing Rate**: ~0.17 FPS (6 seconds per frame)
- **Frame Queue Buffer**: 3 frames
- **Result**: Most frames are dropped, but system remains responsive

**Future Optimization**: Frame skipping (process every Nth frame) or lower camera FPS.

### Memory Usage

- **Frame Data**: ~460KB per frame (480x480x2 bytes)
- **Queue Buffer**: 3 frames × 460KB = ~1.4MB (only when queue full)
- **Notification Queue**: 10 × 140 bytes = 1.4KB (negligible)

### CPU Usage

- **Camera Callback**: Minimal (just memory copy and queue post)
- **Inference Worker**: High CPU during inference (~6s bursts)
- **Display Worker**: Low CPU (mostly idle, waiting for notifications)
- **BLE Worker**: Low CPU (mostly idle, waiting for BLE events)

## API Reference

See the following header files for function declarations:

- `include/queue_types.h` - Queue data structures
- `include/posture_detect.h` - Queue initialization functions
- `include/ble_comm.h` - BLE communication interface
- `include/display_popup.h` - Display popup interface

## Future Enhancements

1. **Frame Skipping**: Process every Nth frame to reduce load
2. **Priority Frames**: Prioritize frames with detected posture
3. **Event System**: Add `os_event` for status broadcasts (optional)
4. **Queue Statistics**: Monitor queue depths and frame drop rates
5. **Adaptive Processing**: Adjust frame processing rate based on posture status
