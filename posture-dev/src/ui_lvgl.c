#include "tal_api.h"
#include "tkl_output.h"
#include "board_com_api.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include <stdio.h>

#include "ai_classifier.h"


// TESTING MESSAGES
static const char* g_call_msgs[] = {
    "Missed call — call me back when you can",
    "Can you hop on a call ASAP?",
    "Quick call later today?",
    "Call me when you see this, urgent",
    "No rush — can we talk sometime this afternoon?"
};

static const char* g_text_msgs[] = {
    "yo u free later?",
    "pls send the doc when u get a sec",
    "asap: we need to decide now",
    "lol ok",
    "hey heads up meeting moved to 4pm"
};

static const char* g_email_msgs[] = {
    "Following up on the deliverables for this week.",
    "Reminder: Please review the attached document by EOD.",
    "Urgent: The demo is blocked; immediate action required.",
    "Newsletter: Top picks for you this week.",
    "Calendar update: Design Review moved to 3:00pm."
};

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))


/* -----------------------------
 * Notification model
 * ----------------------------- */
typedef enum {
    NOTIF_TEXT = 0,
    NOTIF_CALL,
    NOTIF_EMAIL,
} notif_type_t;

typedef struct {
    notif_type_t type;
    const char * title;
    const char * detail;
    const char * msg_text;      // chosen message (for classifier/debug if needed)
    ai_priority_t priority;     // LOW/MED/HIGH
    uint32_t test_index;        // index in the test array
} notif_t;


/* Root UI objects */
static lv_obj_t * g_list_cont = NULL;
static lv_obj_t * g_bottom_bar = NULL;
static lv_obj_t * g_posture_bar = NULL;


/* Simple counters so you can see events */
static uint32_t g_call_cnt  = 0;
static uint32_t g_text_cnt  = 0;
static uint32_t g_email_cnt = 0;

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

static lv_color_t priority_to_color(ai_priority_t p)
{
    switch (p) {
        case AI_PRIORITY_HIGH:   return lv_color_hex(0xFF3B30); // red
        case AI_PRIORITY_MEDIUM: return lv_color_hex(0xFFCC00); // yellow
        case AI_PRIORITY_LOW:
        default:                 return lv_color_hex(0x34C759); // green
    }
}

typedef enum {
    POSTURE_UNKNOWN = 0,
    POSTURE_GOOD,
    POSTURE_BAD,
} posture_state_t;

static posture_state_t posture_get_state(void)
{
    return POSTURE_GOOD;   // TODO: replace with your real flag
}

static lv_color_t posture_to_color(posture_state_t s)
{
    switch (s) {
        case POSTURE_GOOD:    return lv_color_hex(0x34C759); // green
        case POSTURE_BAD:     return lv_color_hex(0xFF3B30); // red
        case POSTURE_UNKNOWN:
        default:              return lv_color_hex(0x8E8E93); // gray
    }
}

void ui_set_posture_state(int state)
{
    if (!g_posture_bar) return;

    posture_state_t s = POSTURE_UNKNOWN;
    if (state == 1) s = POSTURE_GOOD;
    else if (state == 2) s = POSTURE_BAD;

    lv_vendor_disp_lock();
    lv_obj_set_style_bg_color(g_posture_bar, posture_to_color(s), 0);
    lv_vendor_disp_unlock();
}

/* -----------------------------
 * Smooth dismiss animation (your existing logic)
 * ----------------------------- */
typedef struct {
    lv_obj_t * card;
    lv_coord_t start_h;
} collapse_ctx_t;

static void collapse_anim_cb(void * var, int32_t v)
{
    collapse_ctx_t * ctx = (collapse_ctx_t *)var;
    if (!ctx || !ctx->card) return;

    lv_obj_set_height(ctx->card, (lv_coord_t)v);

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

static void notif_card_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * card = (lv_obj_t *)lv_event_get_target(e);
    if (!card) return;

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
    lv_anim_set_time(&a, 220);                 /* slightly longer = smoother */
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, collapse_anim_ready_cb);
    lv_anim_set_user_data(&a, ctx);

    lv_anim_start(&a);
}

/* -----------------------------
 * Card creation
 * ----------------------------- */
static lv_obj_t * notif_card_create(lv_obj_t * parent, const notif_t * n)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);

    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_20, 0);

    /* Stack labels vertically */
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_add_event_cb(card, notif_card_event_cb, LV_EVENT_CLICKED, NULL);

    // added for priority
    /* Top row: title (left) + priority square (right) */
    lv_obj_t * top_row = lv_obj_create(card);
    lv_obj_set_width(top_row, lv_pct(100));
    lv_obj_set_height(top_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 8, 0);

    lv_obj_set_layout(top_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t * title = lv_label_create(top_row);
    lv_label_set_text(title, n->title ? n->title : notif_type_to_str(n->type));
    lv_obj_set_width(title, lv_pct(85));          /* leave room for square */
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    lv_obj_t * pr = lv_obj_create(top_row);
    lv_obj_set_size(pr, 16, 16);
    lv_obj_set_style_radius(pr, 2, 0);           /* square-ish */
    lv_obj_set_style_border_width(pr, 0, 0);
    lv_obj_set_style_bg_color(pr, priority_to_color(n->priority), 0);
    lv_obj_set_style_bg_opa(pr, LV_OPA_COVER, 0);

    /*
    lv_obj_t * title = lv_label_create(card);
    lv_label_set_text(title, n->title ? n->title : notif_type_to_str(n->type));
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    */


    if (n->detail && n->detail[0]) {
        lv_obj_t * detail = lv_label_create(card);
        lv_label_set_text(detail, n->detail);
        lv_obj_set_width(detail, lv_pct(100));
        lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_opa(detail, LV_OPA_80, 0);
    }

    return card;
}

/* Insert at top so newest notifications appear first */
static void notif_list_add_top(const notif_t * n)
{
    if (!g_list_cont) return;

    lv_obj_t * card = notif_card_create(g_list_cont, n);

    /* Move to top: index 0 */
    lv_obj_move_to_index(card, 0);

    /* Optional: scroll to top so you see the new item immediately */
    lv_obj_scroll_to_y(g_list_cont, 0, LV_ANIM_ON);
}

/* -----------------------------
 * Layout: list area + bottom bar
 * ----------------------------- */
static void ui_create_notification_screen(void)
{
    lv_obj_t * scr = lv_screen_active();

    /* Background: blue */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0066FF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Heights */
    const lv_coord_t bar_h     = 70;  // Call/Text/Email
    const lv_coord_t posture_h = 26;  // posture indicator
    const lv_coord_t bottom_h  = bar_h + posture_h;

    /* Screen size (absolute pixels) */
    const lv_coord_t scr_h = lv_obj_get_height(scr);
    const lv_coord_t scr_w = lv_obj_get_width(scr);

    /* List container fills above BOTH bars */
    g_list_cont = lv_obj_create(scr);
    lv_obj_set_pos(g_list_cont, 0, 0);
    lv_obj_set_size(g_list_cont, scr_w, scr_h - bottom_h);

    lv_obj_set_style_pad_all(g_list_cont, 12, 0);
    lv_obj_set_style_pad_row(g_list_cont, 10, 0);
    lv_obj_set_style_border_width(g_list_cont, 0, 0);
    lv_obj_set_style_bg_opa(g_list_cont, LV_OPA_0, 0);

    lv_obj_set_layout(g_list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_list_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_scroll_dir(g_list_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list_cont, LV_SCROLLBAR_MODE_AUTO);

    /* Bottom button bar: sits directly above posture bar */
    g_bottom_bar = lv_obj_create(scr);
    lv_obj_set_pos(g_bottom_bar, 0, scr_h - bottom_h);
    lv_obj_set_size(g_bottom_bar, scr_w, bar_h);

    lv_obj_set_style_pad_all(g_bottom_bar, 10, 0);
    lv_obj_set_style_pad_column(g_bottom_bar, 10, 0);
    lv_obj_set_style_border_width(g_bottom_bar, 0, 0);
    lv_obj_set_style_bg_opa(g_bottom_bar, LV_OPA_40, 0);

    lv_obj_set_layout(g_bottom_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(g_bottom_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_bottom_bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Posture indicator bar: bottom-most */
    g_posture_bar = lv_obj_create(scr);
    lv_obj_set_pos(g_posture_bar, 0, scr_h - posture_h);
    lv_obj_set_size(g_posture_bar, scr_w, posture_h);

    lv_obj_set_style_pad_all(g_posture_bar, 6, 0);
    lv_obj_set_style_border_width(g_posture_bar, 0, 0);
    lv_obj_set_style_radius(g_posture_bar, 0, 0);

    lv_obj_set_style_bg_color(g_posture_bar, posture_to_color(posture_get_state()), 0);
    lv_obj_set_style_bg_opa(g_posture_bar, LV_OPA_COVER, 0);

    lv_obj_t * pl = lv_label_create(g_posture_bar);
    lv_label_set_text(pl, "posture");
    lv_obj_center(pl);
}


/* -----------------------------
 * Bottom buttons
 * ----------------------------- */
static void btn_add_notif_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    notif_type_t t = (notif_type_t)(uintptr_t)lv_event_get_user_data(e);

    notif_t n = {0};
    n.type = t;

    static char title_buf[64];
    static char detail_buf[96];

    const char* chosen_msg = NULL;
    uint32_t chosen_idx = 0;

    switch (t) {
        case NOTIF_CALL: {
            chosen_idx = (uint32_t)(lv_rand(0, (uint32_t)ARR_LEN(g_call_msgs) - 1));
            chosen_msg = g_call_msgs[chosen_idx];
            n.priority = ai_classify_text(chosen_msg);

            snprintf(title_buf, sizeof(title_buf), "Call (%lu)", (unsigned long)chosen_idx);
            n.title = title_buf;

            g_call_cnt++;
            snprintf(detail_buf, sizeof(detail_buf), "Missed call (%lu)", (unsigned long)g_call_cnt);
            n.detail = detail_buf;
            break;
        }
        case NOTIF_TEXT: {
            chosen_idx = (uint32_t)(lv_rand(0, (uint32_t)ARR_LEN(g_text_msgs) - 1));
            chosen_msg = g_text_msgs[chosen_idx];
            n.priority = ai_classify_text(chosen_msg);

            snprintf(title_buf, sizeof(title_buf), "Text (%lu)", (unsigned long)chosen_idx);
            n.title = title_buf;

            g_text_cnt++;
            snprintf(detail_buf, sizeof(detail_buf), "New message (%lu)", (unsigned long)g_text_cnt);
            n.detail = detail_buf;
            break;
        }
        case NOTIF_EMAIL: {
            chosen_idx = (uint32_t)(lv_rand(0, (uint32_t)ARR_LEN(g_email_msgs) - 1));
            chosen_msg = g_email_msgs[chosen_idx];
            n.priority = ai_classify_text(chosen_msg);

            snprintf(title_buf, sizeof(title_buf), "Email (%lu)", (unsigned long)chosen_idx);
            n.title = title_buf;

            g_email_cnt++;
            snprintf(detail_buf, sizeof(detail_buf), "New email (%lu)", (unsigned long)g_email_cnt);
            n.detail = detail_buf;
            break;
        }
        default:
            n.priority = AI_PRIORITY_LOW;
            n.title = "Notification";
            n.detail = "Unknown";
            break;
    }

    n.msg_text = chosen_msg;
    n.test_index = chosen_idx;

    notif_list_add_top(&n);
}


static lv_obj_t * make_bottom_button(lv_obj_t * parent, const char * label_txt, notif_type_t t)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 90, 45);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, label_txt);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, btn_add_notif_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)t);
    return btn;
}

static void ui_create_bottom_buttons(void)
{
    make_bottom_button(g_bottom_bar, "Call",  NOTIF_CALL);
    make_bottom_button(g_bottom_bar, "Text",  NOTIF_TEXT);
    make_bottom_button(g_bottom_bar, "Email", NOTIF_EMAIL);
}

/* -----------------------------
 * Public API (future BLE hook)
 * ----------------------------- */
void ui_add_notification_call(void)
{
    notif_t n = { NOTIF_CALL, "Call", "Missed call" };
    lv_vendor_disp_lock();
    notif_list_add_top(&n);
    lv_vendor_disp_unlock();
}

void ui_add_notification_text(void)
{
    notif_t n = { NOTIF_TEXT, "Text message", "New message received" };
    lv_vendor_disp_lock();
    notif_list_add_top(&n);
    lv_vendor_disp_unlock();
}

void ui_add_notification_email(void)
{
    notif_t n = { NOTIF_EMAIL, "Email", "New email arrived" };
    lv_vendor_disp_lock();
    notif_list_add_top(&n);
    lv_vendor_disp_unlock();
}

void ui_add_notification_from_text(const char *msg, int prio)
{
    if (!msg) return;

    notif_t n = {0};
    n.type = NOTIF_TEXT;            // treat BLE payload as a “text” for now
    n.msg_text = msg;
    n.priority = (ai_priority_t)prio;

    // Use the message itself as the detail
    n.title  = "Message";
    n.detail = msg;

    lv_vendor_disp_lock();
    notif_list_add_top(&n);
    lv_vendor_disp_unlock();
}

/* -----------------------------
 * Entry point
 * ----------------------------- */
void ui_lvgl_start(void)
{
    board_register_hardware();
    lv_vendor_init(DISPLAY_NAME);

    lv_vendor_disp_lock();
    ui_create_notification_screen();
    ui_create_bottom_buttons();
    lv_vendor_disp_unlock();

    lv_vendor_start(5, 1024 * 8);
}
