#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize BLE in central role and start scanning (continuous).
 *
 * Call once after tal_kv_init/tal_sw_timer_init/tal_workq_init are ready.
 */
void ble_central_start(void);

#ifdef __cplusplus
}
#endif
