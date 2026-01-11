#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ui_lvgl_start(void);

/* External trigger API (BLE will call this later) */
void ui_add_notification_call(void);
void ui_add_notification_text(void);
void ui_add_notification_email(void);

void ui_add_notification_from_text(const char *msg, int prio);

void ui_set_posture_state(int state);  // 0=unknown,1=good,2=bad


#ifdef __cplusplus
}
#endif

