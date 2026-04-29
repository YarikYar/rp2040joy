#include "pico/stdlib.h"
#include "pico/time.h"
#include "config.h"
#include "flash_store.h"
#include "input.h"
#include "report.h"
#include "usb_hid.h"

#define POLL_INTERVAL_US  1000   // 1 kHz

int main(void) {
    joy_config_t *cfg = config_get();
    flash_store_load(cfg);

    input_init(cfg);
    usb_hid_init();

    absolute_time_t next_poll = make_timeout_time_us(POLL_INTERVAL_US);

    for (;;) {
        usb_hid_task();

        if (absolute_time_diff_us(get_absolute_time(), next_poll) <= 0) {
            next_poll = delayed_by_us(next_poll, POLL_INTERVAL_US);
            input_poll();

            if (usb_hid_ready()) {
                gamepad_report_t r;
                report_build(&r);
                usb_hid_send(&r);
            }
        }
    }
}
