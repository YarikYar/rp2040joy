#include "input.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"

// Working set lives here. Refreshed by input_reconfigure().
static const joy_config_t *g_cfg;

static int16_t  axis_value[JOY_AXIS_COUNT];
static uint16_t axis_raw[JOY_AXIS_COUNT];
static uint8_t  btn_history[JOY_BUTTON_COUNT];
static uint16_t btn_state;
static uint8_t  hat_value = JOY_HAT_NEUTRAL;
static uint32_t sr_state;     // last shift-register snapshot, bit per input

// Gearbox post-processor state.
//  - gearbox_prev_gear: last *engaged* gear index (0..count-1). Survives
//    transient neutral so a 1→N→2 sweep correctly emits an upshift pulse.
//  - button_pulse_until_us: per-button "OR this bit on while now < deadline".
//  - button_pulse_queue: number of pulses still owed on this button. Used to
//    schedule N spaced pulses for a skip-shift (e.g. 4→2 ⇒ two down pulses).
//  - button_pulse_next_us: time at which the next queued pulse may start.
static int8_t   gearbox_prev_gear = -1;
static uint64_t button_pulse_until_us[JOY_BUTTON_COUNT];
static uint8_t  button_pulse_queue[JOY_BUTTON_COUNT];
static uint64_t button_pulse_next_us[JOY_BUTTON_COUNT];

static inline bool axis_uses_adc(const axis_cfg_t *a, uint8_t *adc_ch) {
    if (!(a->flags & AXIS_FLAG_ENABLE)) return false;
    if (a->source >= AXIS_SOURCE_ADC0 && a->source <= AXIS_SOURCE_ADC3) {
        *adc_ch = a->source - AXIS_SOURCE_ADC0;
        return true;
    }
    return false;
}

static inline bool axis_uses_mux(const axis_cfg_t *a, uint8_t *mux_ch) {
    if (!(a->flags & AXIS_FLAG_ENABLE)) return false;
    if (a->source >= AXIS_SOURCE_MUX_0 && a->source <= AXIS_SOURCE_MUX_7) {
        *mux_ch = a->source - AXIS_SOURCE_MUX_0;
        return true;
    }
    return false;
}

static inline bool mux_active(const mux_cfg_t *m) {
    return (m->flags & MUX_FLAG_ENABLE) &&
           (m->adc_source >= AXIS_SOURCE_ADC0 && m->adc_source <= AXIS_SOURCE_ADC3);
}

static inline bool sr_active(const sr_cfg_t *s) {
    return (s->flags & SR_FLAG_ENABLE) &&
           s->pin_data  != BUTTON_GPIO_NONE &&
           s->pin_clock != BUTTON_GPIO_NONE &&
           s->pin_latch != BUTTON_GPIO_NONE &&
           s->bit_count > 0 && s->bit_count <= SR_MAX_BITS;
}

// Shift in bit_count bits from the chained 74HC165s. Bit 0 of the returned
// word corresponds to the input pin nearest the load latch on the *first*
// chip (i.e. closest to the RP2040 in the chain), matching how a user
// numbers buttons "left to right". The 165 outputs MSB first when clocked,
// so we shift accordingly.
static uint32_t read_shift_register(const sr_cfg_t *s) {
    // Latch parallel inputs: latch low briefly, then high.
    gpio_put(s->pin_latch, 0);
    sleep_us(1);
    gpio_put(s->pin_latch, 1);

    uint32_t value = 0;
    for (uint8_t i = 0; i < s->bit_count; i++) {
        // 74HC165: data bit valid before the rising clock edge.
        // QH presents the bit at the head of the shift register; clocking
        // moves the chain forward by one. Read first, then pulse.
        bool bit = gpio_get(s->pin_data);
        // Bit nearest the load latch on the first chip is read out *last*
        // (chip outputs H7 first, then H6 ...). To map "closest to MCU = bit 0"
        // we accumulate from the top: place each new bit at (bit_count-1-i).
        if (bit) value |= (1u << (s->bit_count - 1 - i));
        gpio_put(s->pin_clock, 1);
        sleep_us(1);
        gpio_put(s->pin_clock, 0);
    }
    return value;
}

// Drive the mux select pins for a given channel and read the configured ADC.
// Caller must guarantee mux_active(); we still check defensively.
static uint16_t read_via_mux(const mux_cfg_t *mux, uint8_t channel) {
    if (!mux_active(mux)) return 0;
    for (uint8_t bit = 0; bit < 3; bit++) {
        uint8_t pin = mux->select_gpio[bit];
        if (pin == BUTTON_GPIO_NONE) continue;
        gpio_put(pin, (channel >> bit) & 1u);
    }
    sleep_us(10);   // 4051 settling time at 3.3V is ≪1µs; 10µs is generous.
    uint8_t adc_ch = mux->adc_source - AXIS_SOURCE_ADC0;
    adc_select_input(adc_ch);
    return adc_read();
}

static void apply_pin_config(const joy_config_t *cfg) {
    // ADC pins: 26 + ch.
    for (uint8_t i = 0; i < JOY_AXIS_COUNT; i++) {
        uint8_t ch;
        if (axis_uses_adc(&cfg->axes[i], &ch) && ch < 4) {
            adc_gpio_init(26 + ch);
        }
    }

    // Mux: init select GPIOs as outputs, init the shared ADC pin.
    if (mux_active(&cfg->mux)) {
        for (uint8_t i = 0; i < 3; i++) {
            uint8_t pin = cfg->mux.select_gpio[i];
            if (pin == BUTTON_GPIO_NONE) continue;
            gpio_init(pin);
            gpio_set_dir(pin, GPIO_OUT);
            gpio_put(pin, 0);
        }
        uint8_t adc_ch = cfg->mux.adc_source - AXIS_SOURCE_ADC0;
        if (adc_ch < 4) adc_gpio_init(26 + adc_ch);
    }

    // Button pins: input + pull-up if active-low. Skip SR-sourced buttons —
    // their bits come from the shift register, not from a GPIO.
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++) {
        const button_cfg_t *b = &cfg->buttons[i];
        if (!(b->flags & BUTTON_FLAG_ENABLE)) continue;
        if (b->flags & BUTTON_FLAG_SHIFT_REG) continue;
        if (b->gpio == BUTTON_GPIO_NONE) continue;
        gpio_init(b->gpio);
        gpio_set_dir(b->gpio, GPIO_IN);
        if (b->flags & BUTTON_FLAG_ACTIVE_LOW) gpio_pull_up(b->gpio);
        else                                   gpio_pull_down(b->gpio);
    }

    // Shift register: latch + clock as outputs, data as input. Idle states:
    // latch high (load disabled), clock low.
    if (sr_active(&cfg->sr)) {
        gpio_init(cfg->sr.pin_latch);
        gpio_set_dir(cfg->sr.pin_latch, GPIO_OUT);
        gpio_put(cfg->sr.pin_latch, 1);

        gpio_init(cfg->sr.pin_clock);
        gpio_set_dir(cfg->sr.pin_clock, GPIO_OUT);
        gpio_put(cfg->sr.pin_clock, 0);

        gpio_init(cfg->sr.pin_data);
        gpio_set_dir(cfg->sr.pin_data, GPIO_IN);
        // No pull on data — it's actively driven by the 165's QH output.
    }

    // HAT pins (always active-low with pull-up).
    if (cfg->hat.flags & HAT_FLAG_ENABLE) {
        const uint8_t pins[4] = {cfg->hat.gpio_up, cfg->hat.gpio_right,
                                  cfg->hat.gpio_down, cfg->hat.gpio_left};
        for (uint8_t i = 0; i < 4; i++) {
            if (pins[i] == BUTTON_GPIO_NONE) continue;
            gpio_init(pins[i]);
            gpio_set_dir(pins[i], GPIO_IN);
            gpio_pull_up(pins[i]);
        }
    }
}

void input_init(const joy_config_t *cfg) {
    adc_init();
    g_cfg = cfg;
    apply_pin_config(cfg);

    for (uint8_t i = 0; i < JOY_AXIS_COUNT; i++) { axis_value[i] = 0; axis_raw[i] = 0; }
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++) {
        btn_history[i] = 0;
        button_pulse_until_us[i] = 0;
        button_pulse_queue[i]    = 0;
        button_pulse_next_us[i]  = 0;
    }
    btn_state = 0;
    hat_value = JOY_HAT_NEUTRAL;
    gearbox_prev_gear = -1;
}

void input_reconfigure(const joy_config_t *cfg) {
    g_cfg = cfg;
    apply_pin_config(cfg);
}

// Map raw 12-bit ADC reading through calibration + deadzone to int16_t.
// raw_min .. raw_center .. raw_max → -32768 .. 0 .. 32767, with a flat deadzone
// of ±deadzone raw units around center.
static int16_t scale_axis(const axis_cfg_t *a, uint16_t raw) {
    int32_t center = a->raw_center;
    int32_t r      = (int32_t)raw;
    int32_t dz     = a->deadzone;
    int32_t out;

    if (r > center + dz) {
        int32_t span = (int32_t)a->raw_max - (center + dz);
        if (span <= 0) return 32767;
        int32_t v = r - (center + dz);
        if (v > span) v = span;
        out = (v * 32767) / span;
    } else if (r < center - dz) {
        int32_t span = (center - dz) - (int32_t)a->raw_min;
        if (span <= 0) return -32768;
        int32_t v = (center - dz) - r;
        if (v > span) v = span;
        out = -((v * 32768) / span);
    } else {
        out = 0;
    }

    if (a->flags & AXIS_FLAG_INVERT) out = -out;
    if (out > 32767)  out = 32767;
    if (out < -32768) out = -32768;
    return (int16_t)out;
}

// Schedule `count` pulses on `btn`, each `pulse_us` long with a `pulse_us`
// gap between them. Subsequent calls add to the queue (a fast 1→3→5
// chain doesn't lose pulses).
static void enqueue_pulses(uint8_t btn, uint8_t count, uint32_t pulse_us, uint64_t now) {
    if (btn >= JOY_BUTTON_COUNT || count == 0) return;
    // If a pulse is already running, queue the rest after it.
    if (button_pulse_until_us[btn] > now) {
        button_pulse_queue[btn]   = (uint8_t)(button_pulse_queue[btn] + count);
        button_pulse_next_us[btn] = button_pulse_until_us[btn] + pulse_us;
    } else {
        button_pulse_until_us[btn] = now + pulse_us;
        if (count > 1) {
            button_pulse_queue[btn]   = (uint8_t)(count - 1);
            button_pulse_next_us[btn] = button_pulse_until_us[btn] + pulse_us;
        }
    }
}

// Step queued pulses forward; called every poll for every button.
static void advance_pulse_queue(uint8_t btn, uint32_t pulse_us, uint64_t now) {
    if (button_pulse_queue[btn] == 0) return;
    if (now < button_pulse_next_us[btn]) return;
    button_pulse_until_us[btn] = now + pulse_us;
    button_pulse_queue[btn]--;
    if (button_pulse_queue[btn] > 0) {
        button_pulse_next_us[btn] = button_pulse_until_us[btn] + pulse_us;
    }
}

// Apply the gearbox transform to btn_state in place. See gearbox_cfg_t doc.
static void apply_gearbox(const gearbox_cfg_t *g) {
    if (!(g->flags & GEARBOX_FLAG_ENABLE)) return;
    if (g->count < 2 || g->count > GEARBOX_MAX_GEARS) return;
    if ((uint16_t)g->first + g->count > JOY_BUTTON_COUNT) return;

    const bool seq   = g->flags & GEARBOX_FLAG_SEQUENTIAL;
    const bool pulse = g->flags & GEARBOX_FLAG_PULSE;
    const uint16_t mask = (uint16_t)(((1u << g->count) - 1u) << g->first);
    const uint16_t gears = btn_state & mask;
    const uint32_t pulse_us = (uint32_t)g->pulse_ms * 1000u;

    // Currently engaged gear: lowest set bit in the mask, or -1 in neutral.
    int8_t cur = -1;
    for (uint8_t i = 0; i < g->count; i++) {
        if (gears & (uint16_t)(1u << (g->first + i))) { cur = (int8_t)i; break; }
    }

    const uint64_t now = time_us_64();

    if (cur >= 0 && cur != gearbox_prev_gear) {
        if (seq) {
            // No pulse on the *first* engagement after boot — we don't know
            // direction. Multi-step shifts (e.g. 4→2) emit |Δgear| pulses.
            if (gearbox_prev_gear >= 0) {
                int delta = (int)cur - (int)gearbox_prev_gear;
                uint8_t  steps = (uint8_t)(delta > 0 ? delta : -delta);
                uint8_t  btn   = (delta > 0) ? g->up_button : g->down_button;
                enqueue_pulses(btn, steps, pulse_us, now);
            }
        } else if (pulse) {
            enqueue_pulses((uint8_t)(g->first + cur), 1, pulse_us, now);
        }
        gearbox_prev_gear = cur;
    }

    // Advance any pending pulse queues (independent of the gearbox bits).
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++) {
        advance_pulse_queue(i, pulse_us, now);
    }

    // Decide what to leave in btn_state for the gear range.
    btn_state &= (uint16_t)~mask;
    if (!seq && !pulse && cur >= 0) {
        // HOLD: re-emit only the engaged gear (mutex).
        btn_state |= (uint16_t)(1u << (g->first + cur));
    }

    // Apply pending pulses on top of the (possibly cleared) btn_state.
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++) {
        if (button_pulse_until_us[i] > now) btn_state |= (uint16_t)(1u << i);
    }
}

static uint8_t hat_from_dirs(bool up, bool right, bool down, bool left) {
    if (up && right)   return JOY_HAT_UP_RIGHT;
    if (right && down) return JOY_HAT_DOWN_RIGHT;
    if (down && left)  return JOY_HAT_DOWN_LEFT;
    if (left && up)    return JOY_HAT_UP_LEFT;
    if (up)            return JOY_HAT_UP;
    if (right)         return JOY_HAT_RIGHT;
    if (down)          return JOY_HAT_DOWN;
    if (left)          return JOY_HAT_LEFT;
    return JOY_HAT_NEUTRAL;
}

void input_poll(void) {
    const joy_config_t *cfg = g_cfg;

    // Axes.
    for (uint8_t i = 0; i < JOY_AXIS_COUNT; i++) {
        const axis_cfg_t *a = &cfg->axes[i];
        uint8_t ch;
        uint16_t raw = 0;
        bool active = false;
        if (axis_uses_adc(a, &ch)) {
            adc_select_input(ch);
            raw = adc_read();
            active = true;
        } else if (axis_uses_mux(a, &ch)) {
            raw = read_via_mux(&cfg->mux, ch);
            active = mux_active(&cfg->mux);
        }
        axis_raw[i]   = active ? raw : 0;
        axis_value[i] = active ? scale_axis(a, raw) : 0;
    }

    // Sample the shift register once per poll into a cache. Each SR-sourced
    // button then just looks up its bit — no per-button clocking.
    if (sr_active(&cfg->sr)) sr_state = read_shift_register(&cfg->sr);
    else                      sr_state = 0;

    // Buttons (debounced).
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++) {
        const button_cfg_t *b = &cfg->buttons[i];
        if (!(b->flags & BUTTON_FLAG_ENABLE)) {
            btn_state &= ~(1u << i);
            continue;
        }
        bool raw_high;
        if (b->flags & BUTTON_FLAG_SHIFT_REG) {
            if (b->gpio >= SR_MAX_BITS || !sr_active(&cfg->sr)) {
                btn_state &= ~(1u << i);
                continue;
            }
            raw_high = (sr_state >> b->gpio) & 1u;
        } else {
            if (b->gpio == BUTTON_GPIO_NONE) {
                btn_state &= ~(1u << i);
                continue;
            }
            raw_high = gpio_get(b->gpio);
        }
        bool pressed = (b->flags & BUTTON_FLAG_ACTIVE_LOW) ? !raw_high : raw_high;
        btn_history[i] = (uint8_t)((btn_history[i] << 1) | (pressed ? 1u : 0u));
        if (btn_history[i] == 0xFF)      btn_state |=  (1u << i);
        else if (btn_history[i] == 0x00) btn_state &= ~(1u << i);
    }

    // Gearbox post-processing (mutex / pulse / sequential).
    apply_gearbox(&cfg->gearbox);

    // HAT.
    if (cfg->hat.flags & HAT_FLAG_ENABLE) {
        bool up    = (cfg->hat.gpio_up    != BUTTON_GPIO_NONE) && !gpio_get(cfg->hat.gpio_up);
        bool right = (cfg->hat.gpio_right != BUTTON_GPIO_NONE) && !gpio_get(cfg->hat.gpio_right);
        bool down  = (cfg->hat.gpio_down  != BUTTON_GPIO_NONE) && !gpio_get(cfg->hat.gpio_down);
        bool left  = (cfg->hat.gpio_left  != BUTTON_GPIO_NONE) && !gpio_get(cfg->hat.gpio_left);
        hat_value = hat_from_dirs(up, right, down, left);
    } else {
        hat_value = JOY_HAT_NEUTRAL;
    }
}

int16_t input_axis(uint8_t i)        { return (i < JOY_AXIS_COUNT) ? axis_value[i] : 0; }
uint16_t input_axis_raw(uint8_t i)   { return (i < JOY_AXIS_COUNT) ? axis_raw[i]   : 0; }
bool    input_button(uint8_t i)      { return (i < JOY_BUTTON_COUNT) && ((btn_state >> i) & 1u); }
uint8_t input_hat(void)              { return hat_value; }
