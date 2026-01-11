#include "ancs_client.h"
#include "tal_api.h"
#include "tal_log.h"
#include <string.h>

/* You may need this include if TKL write is required. If it fails, we’ll adjust. */
#include "tkl_bluetooth.h"

/* 128-bit UUIDs for ANCS (Apple Notification Center Service) */
static const uint8_t UUID_ANCS_SVC[16] = {
    0xD0,0x00,0x2D,0x12,0x1E,0x4B,0x0F,0xA4,0x99,0x4E,0xCE,0xB5,0x31,0xF4,0x05,0x79
};

static const uint8_t UUID_ANCS_NS[16] = { /* Notification Source */
    0xBD,0x1D,0xA2,0x99,0xE6,0x25,0x58,0x8C,0xD9,0x42,0x01,0x63,0x2D,0x12,0xA3,0x1F
};

static const uint8_t UUID_ANCS_CP[16] = { /* Control Point */
    0xD9,0xD9,0xAA,0xFD,0xBD,0x9B,0x21,0x98,0xA8,0x49,0xE1,0x45,0xF3,0xD8,0xD1,0x69
};

static const uint8_t UUID_ANCS_DS[16] = { /* Data Source */
    0xFB,0x7B,0x7C,0xCE,0x6A,0xB3,0x44,0xBE,0xB5,0x4B,0xD6,0x24,0xE9,0xC6,0xEA,0x22
};

static bool uuid128_eq(const uint8_t a[16], const uint8_t b[16]) {
    return memcmp(a, b, 16) == 0;
}

/* Captured handles */
static uint16_t g_conn_handle = 0;
static bool     g_have_conn   = false;

static uint16_t h_ns = 0; /* Notification Source value handle */
static uint16_t h_cp = 0; /* Control Point value handle */
static uint16_t h_ds = 0; /* Data Source value handle */

void ancs_client_init(void)
{
    g_have_conn = false;
    g_conn_handle = 0;
    h_ns = h_cp = h_ds = 0;
}

/* ---- ANCS protocol helpers ---- */

static void ancs_send_get_notif_attrs(uint32_t uid)
{
    if (!g_have_conn || h_cp == 0) {
        PR_WARN("ANCS: cannot request attrs (conn=%d cp_handle=0x%04X)", g_have_conn, h_cp);
        return;
    }

    /* Get Notification Attributes command format:
       [0] CommandID = 0x00
       [1..4] NotificationUID (little-endian)
       Then repeating:
         [AttrID][(optional)MaxLenLE16]
    */

    uint8_t buf[64];
    int i = 0;

    buf[i++] = 0x00; /* CommandID: Get Notification Attributes */
    buf[i++] = (uint8_t)(uid & 0xFF);
    buf[i++] = (uint8_t)((uid >> 8) & 0xFF);
    buf[i++] = (uint8_t)((uid >> 16) & 0xFF);
    buf[i++] = (uint8_t)((uid >> 24) & 0xFF);

    /* Request a minimal useful set:
       0x00 App Identifier (no maxlen)
       0x01 Title (maxlen)
       0x03 Message (maxlen)
    */
    buf[i++] = 0x00; /* App Identifier */

    buf[i++] = 0x01; /* Title */
    buf[i++] = 32;   /* maxlen LSB */
    buf[i++] = 0;    /* maxlen MSB */

    buf[i++] = 0x03; /* Message */
    buf[i++] = 128;  /* maxlen LSB */
    buf[i++] = 0;    /* maxlen MSB */

    /* Write without response to Control Point */
    int rc = tkl_ble_gattc_write_without_rsp(g_conn_handle, h_cp, buf, i);
    PR_NOTICE("ANCS: wrote GetAttrs uid=%u rc=%d (cp=0x%04X len=%d)", (unsigned)uid, rc, h_cp, i);
}

/* Notification Source payload is 8 bytes:
   [0]=EventID [1]=EventFlags [2]=CategoryID [3]=CategoryCount [4..7]=NotificationUID (LE)
*/
static void ancs_handle_ns(const uint8_t *p, uint16_t len)
{
    if (len < 8) return;

    uint8_t  event_id = p[0];
    uint32_t uid = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);

    PR_NOTICE("ANCS:NS event=%u uid=%u", event_id, (unsigned)uid);

    /* EventID 0 = Added, 1 = Modified, 2 = Removed (common mapping) */
    if (event_id == 0 /* Added */ || event_id == 1 /* Modified */) {
        ancs_send_get_notif_attrs(uid);
    }
}

/* Data Source payload is a stream of:
   [CommandID][NotificationUIDLE32][AttrID][AttrLenLE16][AttrValue...][AttrID][AttrLen...]
   For Get Notification Attributes response, CommandID = 0x00.
*/
static void ancs_handle_ds(const uint8_t *p, uint16_t len)
{
    if (len < 1) return;

    /* For hackathon purposes: just dump bytes; later you’ll parse into fields */
    PR_DEBUG("ANCS:DS len=%u", len);
    PR_DEBUG_RAW("ANCS:DS data:");
    for (uint16_t i = 0; i < len; i++) {
        PR_DEBUG_RAW(" %02X", p[i]);
    }
    PR_DEBUG_RAW("\r\n");

    /* Next step (once you see real traffic): implement a reassembly buffer
       because DS responses can be fragmented across notifications. */
}

void ancs_client_request_attrs(uint32_t notif_uid)
{
    ancs_send_get_notif_attrs(notif_uid);
}

/* ---- Hook into Tuya BLE events ---- */

void ancs_client_on_ble_event(const TAL_BLE_EVT_PARAMS_T *evt)
{
    /* The exact event enum names depend on tal_bluetooth_def.h.
       You already see ADV reports; we’ll extend to connect/discovery + gatt reports.
    */

    switch (evt->type) {

    case TAL_BLE_EVT_CONNECT: /* or whatever your connect event is called */
        if (evt->ble_event.connect.result == 0) {
            g_conn_handle = evt->ble_event.connect.peer.conn_handle;
            g_have_conn = true;
            PR_NOTICE("ANCS: connected conn_handle=%u", g_conn_handle);
        }
        break;

    case TAL_BLE_EVT_CENTRAL_CONNECT_DISCOVERY:
        /* This is important: discovery completed. By now your stack may have
           auto-enabled CCCDs during desc discovery. */
        PR_NOTICE("ANCS: discovery complete (ns=0x%04X cp=0x%04X ds=0x%04X)", h_ns, h_cp, h_ds);
        break;

    case TAL_BLE_EVT_CHAR_DISCOVERY: {
        /* You need to confirm the exact field names in your TAL_BLE_CHAR_DISC struct.
           Typical contents: conn_handle, char_handle/value_handle, uuid, properties.
           Your tal_bluetooth.c shows char discovery occurs and then descriptor discovery follows.
        */
        const uint8_t *uuid = evt->ble_event.char_disc.uuid;  /* <-- adjust to real field */
        uint16_t val_handle  = evt->ble_event.char_disc.val_handle;

        if (uuid128_eq(uuid, UUID_ANCS_NS)) {
            h_ns = val_handle;
            PR_NOTICE("ANCS: found Notification Source handle=0x%04X", h_ns);
        } else if (uuid128_eq(uuid, UUID_ANCS_CP)) {
            h_cp = val_handle;
            PR_NOTICE("ANCS: found Control Point handle=0x%04X", h_cp);
        } else if (uuid128_eq(uuid, UUID_ANCS_DS)) {
            h_ds = val_handle;
            PR_NOTICE("ANCS: found Data Source handle=0x%04X", h_ds);
        }
        break;
    }

    case TAL_BLE_EVT_DATA_REPORT: {
        /* This is the key: notifications / indications will come in here.
           Your grep shows tal_bluetooth.c populates:
             evt->ble_event.data_report.peer.char_handle[0]
             evt->ble_event.data_report.report.p_data, len
        */
        uint16_t ch = evt->ble_event.data_report.peer.char_handle[0];
        const uint8_t *p = evt->ble_event.data_report.report.p_data;
        uint16_t l = evt->ble_event.data_report.report.len;

        if (ch == h_ns) {
            ancs_handle_ns(p, l);
        } else if (ch == h_ds) {
            ancs_handle_ds(p, l);
        }
        break;
    }

    default:
        break;
    }
}
