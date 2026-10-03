/* Small BLE advertisement decode helpers for the scan feature. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED

/* Company name for a Bluetooth SIG company identifier, or NULL if unknown. */
const char *ble_adv_fmt_company(uint16_t cid);

/* Copy an advertised name, replacing non-printable bytes with '.'. */
void ble_adv_fmt_name(char *dst, size_t cap, const uint8_t *name, size_t len);

#endif /* CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED */
