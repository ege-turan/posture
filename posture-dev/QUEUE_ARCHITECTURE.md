# Queue-Based Multi-Threaded Architecture

This document describes the queue-based multi-threaded architecture implemented for the posture detection application.

## Overview

The application uses a producer-consumer queue pattern to decouple camera capture from inference processing, allowing the camera to continue capturing frames while inference runs asynchronously (~6 seconds per frame). The architecture also includes separate threads for display notifications and BLE communication.

## Architecture Diagram

```
┌─────────────────┐
│   Camera Task   │ (Camera callback context)
│  (Producer)     │
└────────┬────────┘
         │ posts frame_queue_item_t*
         ▼
┌─────────────────────────────────┐
│      Frame Queue                │ (5 items capacity)
│  (frame_queue_item_t* queue)    │
└────────┬────────────────────────┘
         │ fetches frame_queue_item_t*
         ▼
┌─────────────────┐
│ Inference Worker│ (Separate thread, ~6s per frame)
│     Thread      │
└────────┬────────┘
         │ posts notification_queue_item_t
         ▼
┌─────────────────────────────────┐
│   Notification Queue            │ (10 items capacity)
│ (notification_queue_item_t queue)│
└───────┬─────────────────────────┘
        │
        ├─────────────────────────┐
        │                         │
        ▼                         ▼
┌─────────────────┐    ┌─────────────────┐
│ Display Worker  │    │   BLE Worker    │
│     Thread      │    │     Thread      │
└─────────────────┘    └────────┬────────┘
                                │ posts notification_queue_item_t
                                │ (via callback)
                                ▼
                        ┌─────────────────────────────────┐
                        │   Notification Queue            │
                        │ (notification_queue_item_t queue)│
                        └─────────────────────────────────┘
```

## Components

### 1. Queues

#### Frame Queue (`g_frame_queue`)
- **Type**: `QUEUE_HANDLE`
- **Item Type**: `frame_queue_item_t*` (pointer to frame item)
- **Item Size**: `sizeof(frame_queue_item_t*)` (8 bytes on 64-bit systems)
- **Capacity**: 5 items
- **Producer**: Camera callback (`posture_frame_callback`)
- **Consumer**: Inference worker thread (`inference_worker_task`)

**Frame Queue Item Structure**:
```c
typedef struct {
    uint8_t* frame_data;      // YUV422 frame data (allocated, must be freed by consumer)
    int width;                // Frame width in pixels
    int height;               // Frame height in pixels
    uint64_t timestamp_ms;    // Frame timestamp (milliseconds)
} frame_queue_item_t;
```

#### Notification Queue (`g_notify_queue`)
- **Type**: `QUEUE_HANDLE`
- **Item Type**: `notification_queue_item_t` (value type)
- **Item Size**: `sizeof(notification_queue_item_t)` (~148 bytes)
- **Capacity**: 5 items
- **Producers**: 
  - Inference worker thread (posture warnings)
  - BLE worker thread (phone notifications)
- **Consumer**: Display worker thread (`display_worker_task`)

**Notification Queue Item Structure**:
```c
typedef struct {
    enum {
        NOTIFY_TYPE_PHONE_CALL = 0,
        NOTIFY_TYPE_MESSAGE,
        NOTIFY_TYPE_POSTURE_WARNING,
        NOTIFY_TYPE_POSTURE_GOOD,
        NOTIFY_TYPE_SYSTEM_INFO
    } type;
    char message[128];
    uint32_t duration_ms;
    int priority;
} notification_queue_item_t;
```

### 2. Threads

#### Inference Worker Thread (`inference_worker_task`)
- **Priority**: `THREAD_PRIO_2` (medium-high)
- **Stack Size**: 8192 bytes (large stack needed for inference)
- **Function**: Processes frames from frame queue through MoveNet inference
- **Behavior**:
  - Blocks on frame queue until frame is available
  - Processes frame through inference (~6 seconds)
  - Updates global posture state
  - Posts posture warnings to notification queue if bad posture detected
  - Frees frame data after processing

#### Display Worker Thread (`display_worker_task`)
- **Priority**: `THREAD_PRIO_3` (medium)
- **Stack Size**: 4096 bytes
- **Function**: Processes notifications from notification queue and displays popups
- **Behavior**:
  - Blocks on notification queue until notification is available
  - Calls `display_popup_show()` based on notification type
  - Handles different notification types with appropriate durations

#### BLE Worker Thread (`ble_worker_task`)
- **Priority**: `THREAD_PRIO_3` (medium)
- **Stack Size**: 4096 bytes
- **Function**: Monitors BLE for phone notifications and posts them to notification queue
- **Behavior**:
  - Registers callback with BLE communication module
  - Starts BLE communication
  - Processes notifications via callback (`ble_notification_callback`)
  - Posts notifications to notification queue

## Queue Operations

### Queue Creation (Following `os_queue` Example Pattern)

```c
// Create frame queue (stores pointers)
tal_queue_create_init(&g_frame_queue, sizeof(frame_queue_item_t*), 5);

// Create notification queue (stores values)
tal_queue_create_init(&g_notify_queue, sizeof(notification_queue_item_t), 10);
```

### Queue Posting

**Frame Queue** (stores pointers):
```c
frame_queue_item_t* item = malloc(sizeof(frame_queue_item_t));
// ... populate item ...
tal_queue_post(g_frame_queue, &item, 0);  // Pass address of pointer variable
```

**Notification Queue** (stores values):
```c
notification_queue_item_t notify = {0};
// ... populate notify ...
tal_queue_post(g_notify_queue, &notify, 0);  // Pass address of struct
```

### Queue Fetching

**Frame Queue**:
```c
frame_queue_item_t* item = NULL;
tal_queue_fetch(g_frame_queue, &item, 0xFFFFFFFF);  // Block forever
// Use item...
free(item->frame_data);
free(item);
```

**Notification Queue**:
```c
notification_queue_item_t notify = {0};
tal_queue_fetch(g_notify_queue, &notify, 0xFFFFFFFF);  // Block forever
// Use notify...
```

### Queue Cleanup

Following the `os_queue` example pattern:
1. Stop worker threads (set running flags to false)
2. Delete threads using `tal_thread_delete()`
3. Wait for threads to exit (check thread handle is NULL)
4. Drain remaining queue items (if needed)
5. Free queues using `tal_queue_free()`

## Thread Lifecycle

### Thread Creation (Following `os_queue` Example Pattern)

```c
THREAD_CFG_T thread_cfg = {0};
thread_cfg.stackDepth = 8192;
thread_cfg.priority = THREAD_PRIO_2;
thread_cfg.thrdname = "inference_worker";
g_inference_worker_running = true;
tal_thread_create_and_start(&g_inference_worker_thread, NULL, NULL, 
                             inference_worker_task, NULL, &thread_cfg);
```

### Thread Cleanup

```c
// Stop thread
g_inference_worker_running = false;

// Delete thread
tal_thread_delete(g_inference_worker_thread);

// Wait for thread to exit (following os_queue example pattern)
while (g_inference_worker_thread != NULL) {
    tal_system_sleep(100);
}
```

## Data Flow

### Camera Frame Processing

1. **Camera callback** (`posture_frame_callback`) receives frame from camera
2. Allocates `frame_queue_item_t` and copies frame data
3. Posts pointer to frame queue (non-blocking, timeout 0)
4. If queue is full, frame is dropped (camera continues normally)
5. Returns immediately (camera can continue capturing)

### Inference Processing

1. **Inference worker thread** blocks on frame queue
2. Fetches frame item from queue
3. Processes frame through MoveNet inference (~6 seconds)
4. Updates global posture state
5. If bad posture detected, posts notification to notification queue
6. Frees frame data
7. Loops to fetch next frame

### Display Notifications

1. **Display worker thread** blocks on notification queue
2. Fetches notification from queue
3. Calls `display_popup_show()` based on notification type
4. Loops to fetch next notification

### BLE Notifications

1. **BLE worker thread** monitors BLE communication
2. BLE callback (`ble_notification_callback`) receives notification
3. Creates notification queue item
4. Posts to notification queue
5. Display worker thread processes and displays popup

## Benefits

1. **Non-blocking Camera**: Camera can continue capturing while inference runs (~6s)
2. **Frame Buffering**: Queue buffers up to 5 frames, preventing frame loss during processing
3. **Decoupling**: Camera, inference, display, and BLE are decoupled via queues
4. **Concurrency**: Multiple tasks run concurrently (camera capture, inference, display, BLE)
5. **Priority-based**: Different thread priorities ensure inference gets CPU time when needed
6. **Graceful Degradation**: If frame queue is full, frames are dropped (camera continues)

## Memory Management

- **Frame Queue Items**: Allocated in camera callback, freed in inference worker thread
- **Frame Data**: Allocated in camera callback, freed in inference worker thread
- **Notification Queue Items**: Stack-allocated, copied by queue (value type)
- **Thread Cleanup**: Threads properly cleaned up before queue destruction (following `os_queue` example pattern)

## API Functions

### Queue Management

- `posture_detect_queue_init()`: Creates and initializes queues
- `posture_detect_threads_start()`: Creates and starts all worker threads
- `posture_detect_queue_deinit()`: Stops threads and frees queues

### Public API

- `posture_frame_callback()`: Camera frame callback (posts to queue)
- `posture_get_status()`: Get last detected posture status
- `posture_get_pose()`: Get last pose result

## Example Usage

```c
// Initialize queues
posture_detect_queue_init();

// Start worker threads
posture_detect_threads_start();

// Initialize camera
camera_init();

// Start camera with frame callback (posts to queue)
camera_start(posture_frame_callback, nullptr);

// Main loop (monitor status)
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
```
