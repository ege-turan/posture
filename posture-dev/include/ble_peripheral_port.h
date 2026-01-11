#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_rx_cb_t)(const uint8_t* data, uint16_t len);

void ble_peripheral_port_start(void);
void ble_peripheral_port_set_rx_callback(ble_rx_cb_t cb);

/* Optional: send notification back to phone */
int  ble_peripheral_port_notify(const uint8_t* data, uint16_t len);
bool ble_peripheral_port_is_connected(void);
bool ble_peripheral_port_is_subscribed(void);

bool ble_peripheral_port_pop_rx(char* out, uint16_t out_sz);

#ifdef __cplusplus
}
#endif

