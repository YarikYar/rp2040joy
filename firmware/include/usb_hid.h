#ifndef RP2040JOY_USB_HID_H
#define RP2040JOY_USB_HID_H

#include <stdbool.h>
#include "report.h"

void usb_hid_init(void);
void usb_hid_task(void);
bool usb_hid_ready(void);
void usb_hid_send(const gamepad_report_t *r);

#endif
