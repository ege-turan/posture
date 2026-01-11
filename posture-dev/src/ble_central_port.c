#include "tuya_cloud_types.h"

#include "tal_api.h"
#include "tal_bluetooth.h"
#include "tkl_output.h"

#include <string.h>

/**
 * @brief BLE central event callback
 */
static void __ble_central_event_callback(TAL_BLE_EVT_PARAMS_T *p_event)
{
    PR_DEBUG("----------ble_central event callback-------");
    PR_DEBUG("ble_central event is : %d", p_event->type);

    switch (p_event->type) {
    case TAL_BLE_EVT_ADV_REPORT: {
        int i = 0;

        /* Print peer addr and addr type */
        PR_DEBUG_RAW("Scan device peer addr: ");
        for (i = 0; i < 6; i++) {
            PR_DEBUG_RAW("  %d", p_event->ble_event.adv_report.peer_addr.addr[i]);
        }
        PR_DEBUG_RAW(" \r\n");

        if (TAL_BLE_ADDR_TYPE_RANDOM == p_event->ble_event.adv_report.peer_addr.type) {
            PR_DEBUG("Peer addr type is random address");
        } else {
            PR_DEBUG("Peer addr type is public address");
        }

        /* Print ADV type */
        switch (p_event->ble_event.adv_report.adv_type) {
        case TAL_BLE_ADV_DATA:
            PR_DEBUG("ADV data only!");
            break;
        case TAL_BLE_RSP_DATA:
            PR_DEBUG("Scan Response Data only!");
            break;
        case TAL_BLE_ADV_RSP_DATA:
            PR_DEBUG("ADV data and Scan Response Data!");
            break;
        default:
            PR_DEBUG("ADV type unknown: %d", p_event->ble_event.adv_report.adv_type);
            break;
        }

        /* Print RSSI */
        PR_DEBUG("RSSI : %d", p_event->ble_event.adv_report.rssi);

        /* Print ADV data */
        PR_DEBUG("Advertise packet data length : %d", p_event->ble_event.adv_report.data_len);
        PR_DEBUG_RAW("Advertise packet data: ");
        for (i = 0; i < p_event->ble_event.adv_report.data_len; i++) {
            PR_DEBUG_RAW("  0x%02X", p_event->ble_event.adv_report.p_data[i]);
        }
        PR_DEBUG_RAW(" \r\n");

        break;
    }
    default:
        break;
    }

    // IMPORTANT:
    // Do NOT stop scanning here; we want continuous scan output in posture_dev.
    // tal_ble_scan_stop();
}

/**
 * @brief Start BLE central scanning.
 *
 * Assumes the app already called:
 *   - tal_log_init(...)
 *   - tal_kv_init(...)
 *   - tal_sw_timer_init()
 *   - tal_workq_init()
 */
void ble_central_start(void)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_BLE_SCAN_PARAMS_T scan_cfg;
    memset(&scan_cfg, 0, sizeof(scan_cfg));

    PR_NOTICE("BLE central init start");

    // Initialize BLE stack in central role
    rt = tal_ble_bt_init(TAL_BLE_ROLE_CENTRAL, __ble_central_event_callback);
    if (rt != OPRT_OK) {
        PR_ERR("tal_ble_bt_init failed: %d", rt);
        return;
    }

    // Start scanning
    scan_cfg.type = TAL_BLE_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_interval = 0x400;
    scan_cfg.scan_window = 0x400;
    scan_cfg.timeout = 0xFFFF;   // long scan
    scan_cfg.filter_dup = 0;

    rt = tal_ble_scan_start(&scan_cfg);
    if (rt != OPRT_OK) {
        PR_ERR("tal_ble_scan_start failed: %d", rt);
        return;
    }

    PR_NOTICE("BLE central init success; scanning...");
}
