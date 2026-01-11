#include "tal_api.h"
#include "tkl_output.h"
#include "board_com_api.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include "tal_api.h"
#include "tkl_output.h"
#include "board_com_api.h"

#include "lvgl.h"
#include "lv_vendor.h"

/* -----------------------------
 * Notification model (demo)
 * ----------------------------- */
typedef enum {
    NOTIF_TEXT = 0,
    NOTIF_CALL,
    NOTIF_EMAIL,
} notif_type_t;

typedef struct {
    notif_type_t type;
    const char * title;   // "Text", "Call", "Email"
    const char * detail;  // optional second line later
} notif_t;

/* Root UI objects */
static lv_obj_t * g_list_cont = NULL;

/* -----------------------------
 * Helpers
 * ----------------------------- */
static const char * notif_type_to_str(notif_type_t t)
{
    switch (t) {
        case NOTIF_TEXT:  return "Text message";
        case NOTIF_CALL:  return "Call";
        case NOTIF_EMAIL: return "Email";
        default:          return "Notification";
    }
}

typedef struct {
    lv_obj_t * card;
    lv_coord_t start_h;
} collapse_ctx_t;

static void collapse_anim_cb(void * var, int32_t v)
{
    collapse_ctx_t * ctx = (collapse_ctx_t *)var;
    if (!ctx || !ctx->card) return;

    lv_obj_set_height(ctx->card, (lv_coord_t)v);

    /* fade out proportional to height */
    lv_opa_t opa = (ctx->start_h > 0) ? (lv_opa_t)((v * 255) / ctx->start_h) : 0;
    lv_obj_set_style_opa(ctx->card, opa, 0);
}

static void collapse_anim_ready_cb(lv_anim_t * a)
{
    collapse_ctx_t * ctx = (collapse_ctx_t *)a->user_data;
    if (ctx && ctx->card) {
        lv_obj_delete(ctx->card);
    }
    lv_free(ctx);
}

/* Tap-to-dismiss callback */
static void notif_card_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * card = (lv_obj_t *)lv_event_get_target(e);
    if (!card) return;

    /* Prevent double-click */
    lv_obj_remove_event_cb(card, notif_card_event_cb);

    collapse_ctx_t * ctx = (collapse_ctx_t *)lv_malloc(sizeof(collapse_ctx_t));
    if (!ctx) { lv_obj_delete(card); return; }

    ctx->card = card;
    ctx->start_h = lv_obj_get_height(card);
    if (ctx->start_h <= 0) ctx->start_h = 1;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ctx);
    lv_anim_set_exec_cb(&a, collapse_anim_cb);
    lv_anim_set_values(&a, ctx->start_h, 0);
    lv_anim_set_time(&a, 180);          /* smooth but snappy */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, collapse_anim_ready_cb);
    lv_anim_set_user_data(&a, ctx);

    lv_anim_start(&a);
}


static lv_obj_t * notif_card_create(lv_obj_t * parent, const notif_t * n)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);

    /* Card styling */
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_20, 0);

    /* IMPORTANT: stack children vertically so labels don't overlap */
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    /* Tap to dismiss */
    lv_obj_add_event_cb(card, notif_card_event_cb, LV_EVENT_CLICKED, NULL);

    /* Title label */
    lv_obj_t * title = lv_label_create(card);
    lv_label_set_text(title, n->title ? n->title : notif_type_to_str(n->type));
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);

    /* Detail label */
    if (n->detail && n->detail[0]) {
        lv_obj_t * detail = lv_label_create(card);
        lv_label_set_text(detail, n->detail);
        lv_obj_set_width(detail, lv_pct(100));
        lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_opa(detail, LV_OPA_80, 0);
    }

    return card;
}


/* Create the main notification list container */
static void ui_create_notification_list(void)
{
    lv_obj_t * scr = lv_screen_active();

    /* Background: blue */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0066FF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    g_list_cont = lv_obj_create(scr);
    lv_obj_set_size(g_list_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(g_list_cont, 12, 0);
    lv_obj_set_style_border_width(g_list_cont, 0, 0);
    lv_obj_set_style_bg_opa(g_list_cont, LV_OPA_0, 0);

    /* Vertical list layout */
    lv_obj_set_layout(g_list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list_cont, 10, 0);

    /* Optional: allow scrolling if many notifications */
    lv_obj_set_scroll_dir(g_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list_cont, LV_SCROLLBAR_MODE_AUTO);
}


/* Seed three demo notifications in the requested order */
static void seed_demo_notifications(void)
{
    const notif_t demo[] = {
        { NOTIF_TEXT,  "Text message", "New message received" },
        { NOTIF_CALL,  "Call",         "Missed call" },
        { NOTIF_EMAIL, "Email",        "New email arrived" },
    };

    for (unsigned i = 0; i < sizeof(demo)/sizeof(demo[0]); i++) {
        notif_card_create(g_list_cont, &demo[i]);
    }
}

/* Public entry called from main.cpp */
void ui_lvgl_start(void)
{
    /* You can keep this here or move to main.cpp; either way call once */
    board_register_hardware();

    lv_vendor_init(DISPLAY_NAME);

    lv_vendor_disp_lock();
    ui_create_notification_list();
    seed_demo_notifications();
    lv_vendor_disp_unlock();

    lv_vendor_start(5, 1024 * 8);
}
