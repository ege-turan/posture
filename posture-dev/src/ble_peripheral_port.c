#include "ble_peripheral_port.h"

#include "tal_api.h"
#include "tal_bluetooth.h"
#include "tkl_output.h"
#include <string.h>
#include <stdio.h>

static volatile bool g_notify_enabled = false;
static TAL_BLE_PEER_INFO_T sg_peer;
static ble_peripheral_rx_cb_t sg_rx_cb = NULL;

/* Advertising: Flags + Complete Local Name "POST_HACK" */
static uint8_t s_adv_data[] = {
    0x02, 0x01, 0x06,
    0x0A, 0x09, 'P','O','S','T','_','H','A','C','K'
};

static void ble_log_hex(const uint8_t *p, uint16_t len)
{
    /* Avoid huge logs; cap for sanity */
    const uint16_t cap = (len > 64) ? 64 : len;
    for (uint16_t i = 0; i < cap; i++) {
        PR_DEBUG("rx[%u]=0x%02X", (unsigned)i, p[i]);
    }
    if (len > cap) {
        PR_DEBUG("rx truncated: %u bytes total", (unsigned)len);
    }
}

static void __ble_peripheral_event_callback(TAL_BLE_EVT_PARAMS_T *p_event)
{
    PR_DEBUG("ble_peripheral evt=%d", p_event->type);

    switch (p_event->type) {
    case TAL_BLE_STACK_INIT: {
        if (p_event->ble_event.init == 0) {
            TAL_BLE_DATA_T adv = { .len = sizeof(s_adv_data), .p_data = s_adv_data };
            TAL_BLE_DATA_T rsp = { .len = 0, .p_data = NULL };

            tal_system_sleep(200);
            tal_ble_advertising_data_set(&adv, &rsp);

            OPERATE_RET r = tal_ble_advertising_start(TUYAOS_BLE_DEFAULT_ADV_PARAM);
            PR_NOTICE("adv start ret=%d", (int)r);
        } else {
            PR_ERR("BLE stack init failed: %d", (int)p_event->ble_event.init);
        }
        break;
    }

    case TAL_BLE_EVT_PERIPHERAL_CONNECT: {
        PR_NOTICE("connect result=%d", (int)p_event->ble_event.connect.result);
        if (p_event->ble_event.connect.result == 0) {
            memcpy(&sg_peer, &p_event->ble_event.connect.peer, sizeof(TAL_BLE_PEER_INFO_T));

            /* Optional: populate the common read characteristic with something */
            const char *hello = "CONNECTED";
            TAL_BLE_DATA_T rd = { .len = (uint16_t)strlen(hello), .p_data = (uint8_t *)hello };
            tal_ble_server_common_read_update(&rd);
        } else {
            memset(&sg_peer, 0, sizeof(sg_peer));
        }
        break;
    }

    case TAL_BLE_EVT_DISCONNECT: {
        PR_NOTICE("disconnect; restarting adv");
        g_notify_enabled = false;
        memset(&sg_peer, 0, sizeof(sg_peer));
        tal_ble_advertising_start(TUYAOS_BLE_DEFAULT_ADV_PARAM);
        break;
    }

    case TAL_BLE_EVT_MTU_REQUEST: {
        PR_NOTICE("mtu=%d", (int)p_event->ble_event.exchange_mtu.mtu);
        break;
    }

    case TAL_BLE_EVT_SUBSCRIBE: {
        bool notify_now = (p_event->ble_event.subscribe.cur_notify != 0);
        PR_NOTICE("subscribe char=0x%04X notify:%d->%d indicate:%d->%d",
                  p_event->ble_event.subscribe.char_handle,
                  p_event->ble_event.subscribe.prev_notify,
                  p_event->ble_event.subscribe.cur_notify,
                  p_event->ble_event.subscribe.prev_indicate,
                  p_event->ble_event.subscribe.cur_indicate);

        g_notify_enabled = notify_now;

        if (g_notify_enabled) {
            const char *ack = "NOTIFY ENABLED";
            TAL_BLE_DATA_T pkt = { .len = (uint16_t)strlen(ack), .p_data = (uint8_t *)ack };
            tal_ble_server_common_send(&pkt);
        }
        break;
    }

    case TAL_BLE_EVT_WRITE_REQ: {
        const uint16_t len = p_event->ble_event.write_report.report.len;
        const uint8_t *p   = p_event->ble_event.write_report.report.p_data;

        PR_NOTICE("WRITE_REQ len=%u", (unsigned)len);
        ble_log_hex(p, len);

        /* Hand to app (your UI/classifier bridge) */
        if (sg_rx_cb) {
            sg_rx_cb(p, len);
        }
        break;
    }

    default:
        break;
    }
}

void ble_peripheral_port_set_rx_callback(ble_peripheral_rx_cb_t cb)
{
    sg_rx_cb = cb;
}

bool ble_peripheral_port_notify_enabled(void)
{
    return g_notify_enabled;
}

int ble_peripheral_port_notify(const uint8_t *data, uint16_t len)
{
    if (!g_notify_enabled || data == NULL || len == 0) {
        return -1;
    }
    TAL_BLE_DATA_T pkt = { .len = len, .p_data = (uint8_t *)data };
    return (int)tal_ble_server_common_send(&pkt);
}

void ble_peripheral_port_start(void)
{
    OPERATE_RET rt = OPRT_OK;
    
    /* BLE example expects these services to be available */
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    /* If you already do these in main.cpp, you can omit duplicates.
       Leaving them here makes the module more “drop-in”. */
    tal_sw_timer_init();
    tal_workq_init();

    PR_NOTICE("BLE peripheral: init");
    TUYA_CALL_ERR_LOG(tal_ble_bt_init(TAL_BLE_ROLE_PERIPERAL, __ble_peripheral_event_callback));

    (void)rt;
}
