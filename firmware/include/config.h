#ifndef RP2040JOY_CONFIG_H
#define RP2040JOY_CONFIG_H

#include <stdint.h>
#include "report.h"

// Bumped on any wire-format change. Qt app must match.
#define CONFIG_VERSION   3

// axis_cfg_t.source values.
#define AXIS_SOURCE_NONE  0
#define AXIS_SOURCE_ADC0  1
#define AXIS_SOURCE_ADC1  2
#define AXIS_SOURCE_ADC2  3
#define AXIS_SOURCE_ADC3  4   // GPIO 29 — also VSYS sense on Pico boards, use with care.
// 4051-style 8-channel analog multiplexer. The mux output is wired to a single
// ADC pin (configured in mux_cfg_t.adc_source); 3 select GPIOs pick the channel.
#define AXIS_SOURCE_MUX_0  5
#define AXIS_SOURCE_MUX_1  6
#define AXIS_SOURCE_MUX_2  7
#define AXIS_SOURCE_MUX_3  8
#define AXIS_SOURCE_MUX_4  9
#define AXIS_SOURCE_MUX_5  10
#define AXIS_SOURCE_MUX_6  11
#define AXIS_SOURCE_MUX_7  12

// axis_cfg_t.flags bits.
#define AXIS_FLAG_ENABLE  0x01
#define AXIS_FLAG_INVERT  0x02

typedef struct __attribute__((packed)) {
    uint8_t  source;
    uint8_t  flags;
    uint16_t raw_min;       // ADC raw value at min travel
    uint16_t raw_center;    // ADC raw value at rest
    uint16_t raw_max;       // ADC raw value at max travel
    uint16_t deadzone;      // raw units around center mapped to 0
} axis_cfg_t;

#define BUTTON_GPIO_NONE    0xFF
#define BUTTON_FLAG_ENABLE     0x01
#define BUTTON_FLAG_ACTIVE_LOW 0x02
// When set, the `gpio` field is interpreted as a bit index (0..31) into the
// chained 74HC165 shift register, not as a GPIO pin.
#define BUTTON_FLAG_SHIFT_REG  0x04

typedef struct __attribute__((packed)) {
    uint8_t gpio;     // 0xFF = unused; with BUTTON_FLAG_SHIFT_REG set: SR bit index
    uint8_t flags;
} button_cfg_t;

#define HAT_FLAG_ENABLE  0x01

typedef struct __attribute__((packed)) {
    uint8_t gpio_up;
    uint8_t gpio_right;
    uint8_t gpio_down;
    uint8_t gpio_left;
    uint8_t flags;
    uint8_t _reserved[3];
} hat_cfg_t;

#define MUX_FLAG_ENABLE  0x01

typedef struct __attribute__((packed)) {
    uint8_t adc_source;          // AXIS_SOURCE_ADC0..3 — which ADC reads the mux output
    uint8_t select_gpio[3];      // S0, S1, S2 (low → high bit). 0xFF = unused.
    uint8_t flags;
    uint8_t _reserved[3];
} mux_cfg_t;

// Chained 74HC165 parallel-in / serial-out shift registers. Up to 4 chips
// in series → 32 bits read every poll. Three GPIOs are spent total
// regardless of chain length.
#define SR_FLAG_ENABLE  0x01
#define SR_MAX_BITS     32

typedef struct __attribute__((packed)) {
    uint8_t pin_data;    // QH from the last chip in the chain → RP2040 GPIO (input)
    uint8_t pin_clock;   // CP — clock pulse from RP2040 (output)
    uint8_t pin_latch;   // PL/SH — parallel load, active-low (output)
    uint8_t bit_count;   // 8 / 16 / 24 / 32 — actual chain length
    uint8_t flags;
    uint8_t _reserved[3];
} sr_cfg_t;

// H-pattern shifter post-processing. Treats a contiguous range of buttons
// (e.g. buttons[8..14] for gears 1-7) as a gearbox. Three modes:
//   HOLD       — natural H-pattern: only the engaged gear's button is held
//                (mutex enforced, glitches suppressed).
//   PULSE      — each engagement emits a `pulse_ms` pulse on that gear's
//                button; the held state is suppressed.
//   SEQUENTIAL — gears collapse into two virtual buttons "shift up" and
//                "shift down". On gear change the appropriate one pulses;
//                gear buttons themselves are never reported.
//
// Acts on the *debounced* button state, so the underlying wiring (direct
// GPIO, shift register, active-low) is orthogonal.
#define GEARBOX_FLAG_ENABLE      0x01
#define GEARBOX_FLAG_PULSE       0x02
#define GEARBOX_FLAG_SEQUENTIAL  0x04   // takes priority over PULSE if both set
#define GEARBOX_MAX_GEARS        8

#define GEARBOX_BUTTON_NONE      0xFF

typedef struct __attribute__((packed)) {
    uint8_t first;        // index of the first gear button (0..15)
    uint8_t count;        // 2..GEARBOX_MAX_GEARS gears
    uint8_t pulse_ms;     // pulse length 10..250 (used by PULSE/SEQUENTIAL)
    uint8_t flags;
    uint8_t up_button;    // SEQUENTIAL: button index to pulse on upshift,   0xFF=disabled
    uint8_t down_button;  // SEQUENTIAL: button index to pulse on downshift, 0xFF=disabled
    uint8_t _reserved[2];
} gearbox_cfg_t;

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t poll_hz;                          // 100..1000
    axis_cfg_t   axes[JOY_AXIS_COUNT];
    button_cfg_t buttons[JOY_BUTTON_COUNT];
    hat_cfg_t    hat;
    mux_cfg_t    mux;
    sr_cfg_t     sr;
    gearbox_cfg_t gearbox;
    uint32_t crc32;                            // computed over all preceding bytes
} joy_config_t;

void config_set_defaults(joy_config_t *c);
uint32_t config_crc32(const joy_config_t *c);

// Returns the singleton in-RAM config (loaded at boot).
joy_config_t *config_get(void);

#endif
