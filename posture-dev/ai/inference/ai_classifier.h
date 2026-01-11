#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AI_PRIORITY_LOW = 0,
    AI_PRIORITY_MEDIUM = 1,
    AI_PRIORITY_HIGH = 2,
} ai_priority_t;

ai_priority_t ai_classify_text(const char *text);

#ifdef __cplusplus
}
#endif
