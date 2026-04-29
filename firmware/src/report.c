#include "report.h"
#include "input.h"

void report_build(gamepad_report_t *r) {
    for (uint8_t i = 0; i < JOY_AXIS_COUNT; i++) {
        r->axis[i] = input_axis(i);
    }
    r->hat     = input_hat();
    r->buttons = 0;
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++) {
        if (input_button(i)) r->buttons |= (uint16_t)(1u << i);
    }
}
