/**
 * @file display_popup.c
 * @brief Display popup message implementation (temporary stub)
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 * 
 * TODO: Implement actual display popup functionality
 */

#include "display_popup.h"
#include "tal_api.h"
#include "tkl_output.h"

static bool s_popup_visible = false;

OPERATE_RET display_popup_show(const char* message, uint32_t duration_ms, int priority)
{
    (void)duration_ms;
    (void)priority;
    
    if (message == NULL) {
        return OPRT_INVALID_PARM;
    }
    
    PR_NOTICE("[POPUP] %s (duration: %u ms, priority: %d)", message, duration_ms, priority);
    s_popup_visible = true;
    
    // TODO: Render popup overlay on display
    // - Draw semi-transparent background
    // - Draw message text
    // - Handle priority (override lower priority popups)
    // - Set timer for auto-hide after duration_ms
    
    return OPRT_OK;
}

OPERATE_RET display_popup_hide(void)
{
    if (!s_popup_visible) {
        return OPRT_OK;
    }
    
    PR_DEBUG("[POPUP] Hiding popup");
    s_popup_visible = false;
    
    // TODO: Remove popup overlay from display
    // - Clear popup area
    // - Restore underlying display content
    
    return OPRT_OK;
}

bool display_popup_is_visible(void)
{
    return s_popup_visible;
}
