#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_peripheral_rx_cb_t)(const uint8_t *data, uint16_t len);

/**
 * Start BLE in peripheral role, set advertising payload, and begin advertising.
 * Safe to call once at boot.
 */
void ble_peripheral_port_start(void);

/**
 * Register a callback fired on write requests from the central (phone).
 */
void ble_peripheral_port_set_rx_callback(ble_peripheral_rx_cb_t cb);

/**
 * Returns whether notifications have been enabled by the central (CCCD written).
 */
bool ble_peripheral_port_notify_enabled(void);

/**
 * Send a notification on the “common server” characteristic (if subscribed).
 * Returns 0 on success, nonzero on failure.
 */
int ble_peripheral_port_notify(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif
