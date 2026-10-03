#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the camera as an ambient light sensor and start the periodic
 * brightness-adjust task. Call from the worker task after display + i2c
 * are up; failures are logged and leave brightness manual. */
void ambient_init(void);

/* Measured ambient calibration bounds (raw luma). min=255/max=0 means
 * uncalibrated. Safe to call from any task. */
void ambient_get_calibration(uint8_t *min_out, uint8_t *max_out);

/* Last raw luma sample (stale while settings are open - ambient pauses). */
uint8_t ambient_get_last_luma(void);

/* Clear measured bounds back to uncalibrated and persist. Call from the
 * LVGL task. */
void ambient_reset_calibration(void);

#ifdef __cplusplus
}
#endif
