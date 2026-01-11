#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "tal_bluetooth_def.h"   // for TAL_BLE_EVT_PARAMS_T, TAL_BLE_PEER_INFO_T

#ifdef __cplusplus
extern "C" {
#endif

void ancs_client_init(void);
void ancs_client_on_ble_event(const TAL_BLE_EVT_PARAMS_T *evt);

/* Optional: helper to request attributes for a known notification UID */
void ancs_client_request_attrs(uint32_t notif_uid);

#ifdef __cplusplus
}
#endif
