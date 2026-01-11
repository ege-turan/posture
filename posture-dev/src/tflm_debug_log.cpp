#include "tensorflow/lite/micro/debug_log.h"
#include "tal_api.h"
#include "tkl_output.h"
#include <cstdarg>
#include <cstdio>

extern "C" {
void DebugLog(const char* format, va_list args) {
#ifndef TF_LITE_STRIP_ERROR_STRINGS
    constexpr int kMaxLogLen = 256;
    char log_buffer[kMaxLogLen];
    
    vsnprintf(log_buffer, kMaxLogLen, format, args);
    PR_ERR("[TFLM] %s", log_buffer);  // Redirect to Tuya logging
#endif
}

int DebugVsnprintf(char* buffer, size_t buf_size, const char* format, va_list vlist) {
    return vsnprintf(buffer, buf_size, format, vlist);
}
}