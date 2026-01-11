#include "tal_api.h"
#include "tkl_output.h"
#include "board_com_api.h"

#include "lvgl.h"
#include "lv_vendor.h"

static void button_event_cb(lv_event_t * e)
{
    static bool toggled = false;
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr,
                              lv_color_hex(toggled ? 0x202020 : 0x0066FF),
                              0);
    toggled = !toggled;
}

void make_center_button(void)
{
    lv_obj_t * btn = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn, 140, 60);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "Toggle");
    lv_obj_center(label);
}


void ui_lvgl_start(void)
{
    // Logging (do once in your app)
    // tal_log_init(...)

    board_register_hardware();

    // DISPLAY_NAME must be defined by Kconfig ("device driver" -> display name)
    lv_vendor_init(DISPLAY_NAME);

    lv_vendor_disp_lock();
    make_center_button();
    lv_vendor_disp_unlock();

    // Start LVGL worker (refresh/input handling)
    lv_vendor_start(5, 1024 * 8);
}
