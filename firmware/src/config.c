#include <stddef.h>
#include <string.h>
#include "config.h"

static joy_config_t g_config;

joy_config_t *config_get(void) {
    return &g_config;
}

void config_set_defaults(joy_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->version = CONFIG_VERSION;
    c->poll_hz = 1000;

    // Axes 0..2 → ADC0..2, 12-bit ADC range, no deadzone.
    for (uint8_t i = 0; i < 3; i++) {
        c->axes[i].source     = AXIS_SOURCE_ADC0 + i;
        c->axes[i].flags      = AXIS_FLAG_ENABLE;
        c->axes[i].raw_min    = 0;
        c->axes[i].raw_center = 2048;
        c->axes[i].raw_max    = 4095;
        c->axes[i].deadzone   = 32;
    }

    // Buttons 0..7 → GPIO 2..9, active low.
    for (uint8_t i = 0; i < 8; i++) {
        c->buttons[i].gpio  = 2 + i;
        c->buttons[i].flags = BUTTON_FLAG_ENABLE | BUTTON_FLAG_ACTIVE_LOW;
    }
    for (uint8_t i = 8; i < JOY_BUTTON_COUNT; i++) {
        c->buttons[i].gpio = BUTTON_GPIO_NONE;
    }

    c->hat.gpio_up = c->hat.gpio_right = c->hat.gpio_down = c->hat.gpio_left = BUTTON_GPIO_NONE;

    c->mux.adc_source = AXIS_SOURCE_NONE;
    c->mux.select_gpio[0] = c->mux.select_gpio[1] = c->mux.select_gpio[2] = BUTTON_GPIO_NONE;
    c->mux.flags = 0;

    c->sr.pin_data  = BUTTON_GPIO_NONE;
    c->sr.pin_clock = BUTTON_GPIO_NONE;
    c->sr.pin_latch = BUTTON_GPIO_NONE;
    c->sr.bit_count = 0;
    c->sr.flags     = 0;

    c->gearbox.first       = 0;
    c->gearbox.count       = 0;
    c->gearbox.pulse_ms    = 50;
    c->gearbox.flags       = 0;
    c->gearbox.up_button   = GEARBOX_BUTTON_NONE;
    c->gearbox.down_button = GEARBOX_BUTTON_NONE;
}

// CRC-32/ISO-HDLC (poly 0xEDB88320, init 0xFFFFFFFF, reflected, xorout 0xFFFFFFFF).
// Same polynomial as zlib/Ethernet. Computed over bytes preceding the crc32 field.
uint32_t config_crc32(const joy_config_t *c) {
    const uint8_t *p = (const uint8_t *)c;
    size_t len = offsetof(joy_config_t, crc32);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}
