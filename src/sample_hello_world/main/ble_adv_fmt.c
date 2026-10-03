/* BLE advertisement decode helpers. See ble_adv_fmt.h. */
#include "ble_adv_fmt.h"

#if CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED

/* Bluetooth SIG company identifiers (common consumer vendors only). */
static const struct {
    uint16_t cid;
    const char *name;
} COMPANIES[] = {
    { 0x0001, "Nokia" },
    { 0x0002, "Intel" },
    { 0x0006, "Microsoft" },
    { 0x000D, "TI" },
    { 0x004C, "Apple" },
    { 0x0059, "Nordic Semi" },
    { 0x0075, "Samsung" },
    { 0x0087, "Garmin" },
    { 0x00E0, "Google" },
    { 0x012D, "Sony" },
    { 0x0157, "Huami" },
    { 0x02E5, "Espressif" },
    { 0x038F, "Xiaomi" },
    { 0x05A7, "Sonos" },
};

const char *ble_adv_fmt_company(uint16_t cid)
{
    for (size_t i = 0; i < sizeof COMPANIES / sizeof COMPANIES[0]; i++) {
        if (COMPANIES[i].cid == cid) {
            return COMPANIES[i].name;
        }
    }
    return NULL;
}

void ble_adv_fmt_name(char *dst, size_t cap, const uint8_t *name, size_t len)
{
    if (cap == 0) {
        return;
    }
    size_t n = len < cap - 1 ? len : cap - 1;
    for (size_t i = 0; i < n; i++) {
        dst[i] = (name[i] >= 0x20 && name[i] <= 0x7E) ? (char)name[i] : '.';
    }
    dst[n] = '\0';
}

#endif /* CONFIG_BT_ENABLED && CONFIG_BT_NIMBLE_ENABLED */
