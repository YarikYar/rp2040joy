#ifndef RP2040JOY_REPORT_H
#define RP2040JOY_REPORT_H

#include <stdint.h>

#define HID_REPORT_ID_INPUT  0x01

#define JOY_AXIS_COUNT    6
#define JOY_BUTTON_COUNT  16

#define JOY_HAT_UP        0
#define JOY_HAT_UP_RIGHT  1
#define JOY_HAT_RIGHT     2
#define JOY_HAT_DOWN_RIGHT 3
#define JOY_HAT_DOWN      4
#define JOY_HAT_DOWN_LEFT 5
#define JOY_HAT_LEFT      6
#define JOY_HAT_UP_LEFT   7
#define JOY_HAT_NEUTRAL   8

#pragma pack(push, 1)
typedef struct {
    int16_t  axis[JOY_AXIS_COUNT];   // -32768..32767, order: X Y Z Rx Ry Rz
    uint8_t  hat;                    // 0..7 direction, 8 neutral
    uint16_t buttons;                // bit N = button N
} gamepad_report_t;
#pragma pack(pop)

void report_build(gamepad_report_t *r);

#endif
