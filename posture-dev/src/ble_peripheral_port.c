#include "ble_peripheral_port.h"

#include "tkl_output.h"
#include "tkl_bluetooth.h"   // TKL BLE APIs (GAP + GATT server)
#include <string.h>
#include <stdio.h>

#include "tal_api.h"

/* ----------------------------
 *  Custom 128-bit UUIDs
 *  (Generate your own later; these are fine for hackathon testing.)
 *  Service:  6E400001-B5A3-F393-E0A9-E50E24DCCA9E  (Nordic UART style)
 *  RX Char:  6E400002-B5A3-F393-E0A9-E50E24DCCA9E  (Write)
 *  TX Char:  6E400003-B5A3-F393-E0A9-E50E24DCCA9E  (Notify)
 * ---------------------------- */

static const uint8_t UUID_SVC[16] = { 0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x01,0x00,0x40,0x6E };
static const uint8_t UUID_RX [16] = { 0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x02,0x00,0x40,0x6E };
static const uint8_t UUID_TX [16] = { 0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x03,0x00,0x40,0x6E };

static volatile uint16_t s_conn_handle = 0xFFFF;
static volatile bool     s_subscribed = false;

static uint16_t s_svc_handle = 0;
static uint16_t s_rx_handle  = 0;
static uint16_t s_tx_handle  = 0;

static ble_rx_cb_t s_rx_cb = NULL;

static char s_inbox[256];
static volatile bool s_inbox_full = false;

/* ---------- Helpers: build TKL UUID ---------- */
static TKL_BLE_UUID_T make_uuid128(const uint8_t uuid128[16])
{
    TKL_BLE_UUID_T u;
    memset(&u, 0, sizeof(u));
    u.type = TKL_BLE_UUID_TYPE_128;   // name may differ slightly; adjust if compiler complains
    memcpy(u.uuid, uuid128, 16);      // same: adjust field name if needed
    return u;
}

/* ---------- GAP callback ---------- */
static void gap_cb(TKL_BLE_GAP_PARAMS_EVT_T *evt)
{
    switch (evt->type) {
    case TKL_BLE_GAP_EVT_CONNECT:
        if (evt->result == 0) {
            s_conn_handle = evt->conn_handle;
            PR_NOTICE("GAP: connected, conn=0x%04X", s_conn_handle);
        } else {
            PR_NOTICE("GAP: connect failed, res=%d", evt->result);
        }
        break;

    case TKL_BLE_GAP_EVT_DISCONNECT:
        PR_NOTICE("GAP: disconnected, reason=%d", evt->gap_event.disconnect.reason);
        s_conn_handle = 0xFFFF;
        s_subscribed = false;
        /* restart advertising */
        tkl_ble_gap_adv_start(NULL); // if your platform requires params, keep a static params struct
        break;

    default:
        break;
    }
}

/* ---------- GATT callback ---------- */
static void gatt_cb(TKL_BLE_GATT_PARAMS_EVT_T *evt)
{
    /* You will need to match your platform’s GATT event enum names.
       Common ones: WRITE, MTU, SUBSCRIBE/CCCD, etc. */

    switch (evt->type) {

    case TKL_BLE_GATT_EVT_WRITE: {
        if (evt->gatt_event.write.char_handle == s_rx_handle) {
            const uint8_t *p = evt->gatt_event.write.p_data;
            uint16_t len     = evt->gatt_event.write.len;

            PR_NOTICE("GATT: RX write len=%u", len);

            /* Non-blocking inbox: copy data, return immediately */
            if (!s_inbox_full) {
                uint16_t n = (len < sizeof(s_inbox) - 1)
                            ? len
                            : (sizeof(s_inbox) - 1);
                memcpy(s_inbox, p, n);
                s_inbox[n] = '\0';
                s_inbox_full = true;
            }
        }
        break;
    }

    case TKL_BLE_GATT_EVT_CCCD: {
        /* CCCD updates happen when nRF Connect enables notifications */
        if (evt->gatt_event.cccd.char_handle == s_tx_handle) {
            s_subscribed = (evt->gatt_event.cccd.notify_enabled != 0);
            PR_NOTICE("GATT: TX notify subscribed=%d", s_subscribed);
        }
        break;
    }

    default:
        break;
    }
}

/* ---------- Add our UART-like service ---------- */
static int add_uart_service(void)
{
    /* Characteristic definitions */
    static TKL_BLE_CHAR_PARAMS_T chars[2];
    memset(chars, 0, sizeof(chars));

    /* RX (Write) */
    chars[0].uuid = make_uuid128(UUID_RX);
    chars[0].prop = TKL_BLE_CHAR_PROP_WRITE | TKL_BLE_CHAR_PROP_WRITE_NR;
    chars[0].perm = TKL_BLE_PERM_WRITE;              // adjust if names differ
    chars[0].handle = 0xFF;                          // per docs: filled in by stack :contentReference[oaicite:1]{index=1}

    /* TX (Notify) */
    chars[1].uuid = make_uuid128(UUID_TX);
    chars[1].prop = TKL_BLE_CHAR_PROP_NOTIFY;
    chars[1].perm = TKL_BLE_PERM_READ;               // often required for CCCD read
    chars[1].handle = 0xFF;

    static TKL_BLE_SERVICE_PARAMS_T svc;
    memset(&svc, 0, sizeof(svc));
    svc.handle   = 0xFF;
    svc.svc_uuid = make_uuid128(UUID_SVC);
    svc.type     = TKL_BLE_SERVICE_PRIMARY;
    svc.char_num = 2;
    svc.p_char   = chars;

    static TKL_BLE_GATTS_PARAMS_T gatts;
    memset(&gatts, 0, sizeof(gatts));
    gatts.svc_num   = 1;
    gatts.p_service = &svc;

    OPERATE_RET rt = tkl_ble_gatts_service_add(&gatts);
    PR_NOTICE("gatt add service ret=%d", (int)rt);

    /* After add, handles should be updated (doc says handle values are updated after adding) :contentReference[oaicite:2]{index=2} */
    s_svc_handle = svc.handle;
    s_rx_handle  = chars[0].handle;
    s_tx_handle  = chars[1].handle;

    PR_NOTICE("handles: svc=0x%04X rx=0x%04X tx=0x%04X", s_svc_handle, s_rx_handle, s_tx_handle);

    return (rt == OPRT_OK) ? 0 : -1;
}

static void start_advertising(void)
{
    /* Adv payload: Flags + Complete Local Name */
    static uint8_t adv[] = {
        0x02, 0x01, 0x06,
        0x0A, 0x09, 'T','U','Y','A','_','U','A','R','T'
    };
    TKL_BLE_DATA_T adv_data = { .p_data = adv, .len = sizeof(adv) };

    /* No scan response needed */
    TKL_BLE_DATA_T rsp_data = { .p_data = NULL, .len = 0 };

    tkl_ble_gap_adv_rsp_data_set(&adv_data, &rsp_data);
    tkl_ble_gap_adv_start(NULL);  // if your platform needs explicit params, keep a static params struct
    PR_NOTICE("adv started");
}

/* queue */
static char s_inbox[256];
static volatile bool s_inbox_full = false;

bool ble_peripheral_port_pop_rx(char* out, uint16_t out_sz)
{
    if (!s_inbox_full) return false;
    uint16_t n = (uint16_t)strnlen(s_inbox, sizeof(s_inbox));
    if (out_sz == 0) return false;
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, s_inbox, n);
    out[n] = '\0';
    s_inbox_full = false;
    return true;
}


/* ---------- Public API ---------- */
void ble_peripheral_port_set_rx_callback(ble_rx_cb_t cb)
{
    s_rx_cb = cb;
}

bool ble_peripheral_port_is_connected(void)
{
    return (s_conn_handle != 0xFFFF);
}

bool ble_peripheral_port_is_subscribed(void)
{
    return s_subscribed;
}

int ble_peripheral_port_notify(const uint8_t* data, uint16_t len)
{
    if (!ble_peripheral_port_is_connected() || !s_subscribed || !data || !len) return -1;
    return (int)tkl_ble_gatts_value_notify(s_conn_handle, s_tx_handle, (uint8_t*)data, len);
}

void ble_peripheral_port_start(void)
{
    /* Initialize stack as SERVER (per docs) :contentReference[oaicite:3]{index=3} */
    OPERATE_RET rt;

    rt = tkl_ble_stack_init(TKL_BLE_ROLE_SERVER);
    PR_NOTICE("stack init ret=%d", (int)rt);

    rt = tkl_ble_gap_callback_register(gap_cb);
    PR_NOTICE("gap cb reg ret=%d", (int)rt);

    rt = tkl_ble_gatt_callback_register(gatt_cb);
    PR_NOTICE("gatt cb reg ret=%d", (int)rt);

    /* Create GATT services BEFORE advertising so nRF Connect sees them immediately */
    add_uart_service();

    start_advertising();
}
