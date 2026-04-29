#include "tusb.h"
#include "pico/unique_id.h"
#include "report.h"
#include "hid_protocol.h"

#define USB_VID  0xCafe
#define USB_PID  0x4004
#define USB_BCD  0x0200

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

// HID Report Descriptor: Joystick with 6×16-bit axes, 1 HAT, 16 buttons.
static const uint8_t desc_hid_report[] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    0x09, 0x04,                    // Usage (Joystick)
    0xA1, 0x01,                    // Collection (Application)
      0x85, HID_REPORT_ID_INPUT,   //   Report ID

      // 6 × 16-bit axes: X, Y, Z, Rx, Ry, Rz
      0x05, 0x01,                  //   Usage Page (Generic Desktop)
      0x09, 0x30,                  //   Usage (X)
      0x09, 0x31,                  //   Usage (Y)
      0x09, 0x32,                  //   Usage (Z)
      0x09, 0x33,                  //   Usage (Rx)
      0x09, 0x34,                  //   Usage (Ry)
      0x09, 0x35,                  //   Usage (Rz)
      0x16, 0x00, 0x80,            //   Logical Minimum (-32768)
      0x26, 0xFF, 0x7F,            //   Logical Maximum (32767)
      0x75, 0x10,                  //   Report Size (16)
      0x95, 0x06,                  //   Report Count (6)
      0x81, 0x02,                  //   Input (Data, Var, Abs)

      // 1 × HAT switch (4 bits + 4 bits padding)
      0x09, 0x39,                  //   Usage (Hat switch)
      0x15, 0x00,                  //   Logical Minimum (0)
      0x25, 0x07,                  //   Logical Maximum (7)
      0x35, 0x00,                  //   Physical Minimum (0)
      0x46, 0x3B, 0x01,            //   Physical Maximum (315 degrees)
      0x65, 0x14,                  //   Unit (Eng Rot: Degrees)
      0x75, 0x04,                  //   Report Size (4)
      0x95, 0x01,                  //   Report Count (1)
      0x81, 0x42,                  //   Input (Data, Var, Abs, Null state)
      0x65, 0x00,                  //   Unit (None)
      0x75, 0x04,                  //   Report Size (4)
      0x95, 0x01,                  //   Report Count (1)
      0x81, 0x03,                  //   Input (Const, Var, Abs) — padding

      // 16 buttons
      0x05, 0x09,                  //   Usage Page (Button)
      0x19, 0x01,                  //   Usage Minimum (Button 1)
      0x29, 0x10,                  //   Usage Maximum (Button 16)
      0x15, 0x00,                  //   Logical Minimum (0)
      0x25, 0x01,                  //   Logical Maximum (1)
      0x75, 0x01,                  //   Report Size (1)
      0x95, 0x10,                  //   Report Count (16)
      0x81, 0x02,                  //   Input (Data, Var, Abs)

      // --- Feature reports (vendor-defined) -------------------------------
      0x06, 0x00, 0xFF,            //   Usage Page (Vendor 0xFF00)
      0x15, 0x00,                  //   Logical Minimum (0)
      0x26, 0xFF, 0x00,            //   Logical Maximum (255)
      0x75, 0x08,                  //   Report Size (8)

      // CONFIG_BLOCK: 57 data bytes
      0x85, HID_REPORT_ID_CONFIG_BLOCK,
      0x09, 0x01,                  //   Usage (vendor 1)
      0x95, (uint8_t)(sizeof(hid_config_block_t)),
      0xB1, 0x02,                  //   Feature (Data, Var, Abs)

      // COMMAND: 7 data bytes (host -> device)
      0x85, HID_REPORT_ID_COMMAND,
      0x09, 0x02,
      0x95, (uint8_t)(sizeof(hid_command_t)),
      0xB1, 0x02,

      // STATUS: 31 data bytes (device -> host)
      0x85, HID_REPORT_ID_STATUS,
      0x09, 0x03,
      0x95, (uint8_t)(sizeof(hid_status_t)),
      0xB1, 0x02,

      // RAW_INPUT: 15 data bytes (device -> host)
      0x85, HID_REPORT_ID_RAW_INPUT,
      0x09, 0x04,
      0x95, (uint8_t)(sizeof(hid_raw_input_t)),
      0xB1, 0x02,
    0xC0                           // End Collection
};

// HID descriptor's Report Count is a single byte; verify our struct sizes fit.
_Static_assert(sizeof(hid_config_block_t) <= 255, "config block too large for descriptor");
_Static_assert(sizeof(hid_command_t)      <= 255, "command too large for descriptor");
_Static_assert(sizeof(hid_status_t)       <= 255, "status too large for descriptor");
_Static_assert(sizeof(hid_raw_input_t)    <= 255, "raw input too large for descriptor");
_Static_assert(sizeof(hid_config_block_t) <= CFG_TUD_HID_EP_BUFSIZE,
               "CFG_TUD_HID_EP_BUFSIZE must hold the largest feature report");

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

enum { ITF_NUM_HID, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID         0x81

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL };

static const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},   // English (0x0409)
    "rp2040joy",                  // Manufacturer
    "RP2040 Configurable Joystick", // Product
    NULL,                         // Serial — generated from chip unique ID
};

static uint16_t _desc_str[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count = 0;

    if (index == STRID_LANGID) {
        _desc_str[1] = ((const uint16_t *)string_desc_arr[0])[0];
        chr_count = 1;
    } else if (index == STRID_SERIAL) {
        pico_unique_board_id_t uid;
        pico_get_unique_board_id(&uid);
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 0; i < 8; i++) {
            _desc_str[1 + i*2]     = hex[(uid.id[i] >> 4) & 0xF];
            _desc_str[1 + i*2 + 1] = hex[uid.id[i] & 0xF];
        }
        chr_count = 16;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = 0;
        while (str[chr_count] && chr_count < 32) {
            _desc_str[1 + chr_count] = str[chr_count];
            chr_count++;
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
