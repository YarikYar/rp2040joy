#ifndef RP2040JOY_INPUT_H
#define RP2040JOY_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "report.h"
#include "config.h"

// Initialize hardware (ADC + GPIOs) per the supplied config.
// Must be called again (input_reconfigure) if the config changes at runtime.
void input_init(const joy_config_t *cfg);

// Re-apply pin/ADC setup after a config change. Cheap to call.
void input_reconfigure(const joy_config_t *cfg);

// Poll all inputs once. Caller drives the cadence.
void input_poll(void);

int16_t input_axis(uint8_t i);
bool    input_button(uint8_t i);
uint8_t input_hat(void);

// Raw ADC reading for axis i (0..4095). Used by RAW_INPUT report. Returns 0
// if axis disabled.
uint16_t input_axis_raw(uint8_t i);

#endif
