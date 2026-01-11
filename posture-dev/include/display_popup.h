/**
 * @file display_popup.h
 * @brief Display popup message interface (temporary stub)
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 * 
 * TODO: Implement actual display popup functionality
 */

#ifndef DISPLAY_POPUP_H
#define DISPLAY_POPUP_H

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Show a popup message on the display
 * 
 * @param message Message text to display
 * @param duration_ms Duration to show popup in milliseconds (0 = default duration)
 * @param priority Priority level (higher = more urgent, may override other popups)
 * @return OPERATE_RET OPRT_OK on success, error code otherwise
 */
OPERATE_RET display_popup_show(const char* message, uint32_t duration_ms, int priority);

/**
 * @brief Hide the current popup message
 * 
 * @return OPERATE_RET OPRT_OK on success
 */
OPERATE_RET display_popup_hide(void);

/**
 * @brief Check if a popup is currently being displayed
 * 
 * @return true if popup is visible, false otherwise
 */
bool display_popup_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_POPUP_H
