// Shared between firmware and the Qt configuration app.
//
// Everything is little-endian and packed. Sizes are wire sizes — do not let the
// compiler add padding. Bump HID_PROTOCOL_VERSION on any layout change.
#ifndef RP2040JOY_HID_PROTOCOL_H
#define RP2040JOY_HID_PROTOCOL_H

#include <stdint.h>
#include "report.h"
#include "config.h"

#define HID_PROTOCOL_VERSION   1

// --- Report IDs ---------------------------------------------------------
// 0x01 is the input report (gamepad_report_t).
#define HID_REPORT_ID_CONFIG_BLOCK   0x10  // Feature, IN+OUT — chunked config R/W
#define HID_REPORT_ID_COMMAND        0x11  // Feature, OUT     — execute command
#define HID_REPORT_ID_STATUS         0x12  // Feature, IN      — fw/proto version
#define HID_REPORT_ID_RAW_INPUT      0x13  // Feature, IN      — raw ADC + GPIO

// --- Sizes --------------------------------------------------------------
// Each Feature Report has a fixed size (descriptor-defined). Host sends/reads
// a buffer of that size; the report ID is at byte 0 in HID API conventions.
#define HID_CONFIG_BLOCK_PAYLOAD     56   // bytes carried per chunk
#define HID_CONFIG_BLOCK_TOTAL ((sizeof(joy_config_t) + HID_CONFIG_BLOCK_PAYLOAD - 1) \
                                / HID_CONFIG_BLOCK_PAYLOAD)

// --- CONFIG_BLOCK -------------------------------------------------------
// GET: device returns the requested chunk of the live in-RAM config.
// SET: host writes a chunk into the in-RAM config (no flash commit).
// Use CMD_SAVE_TO_FLASH to commit, CMD_RELOAD_FLASH to discard.
#pragma pack(push, 1)
typedef struct {
    uint8_t block_index;     // 0 .. HID_CONFIG_BLOCK_TOTAL - 1
    uint8_t block_total;     // == HID_CONFIG_BLOCK_TOTAL (sanity)
    uint8_t payload[HID_CONFIG_BLOCK_PAYLOAD];
} hid_config_block_t;

// --- COMMAND ------------------------------------------------------------
enum {
    CMD_NOP            = 0,
    CMD_SAVE_TO_FLASH  = 1,
    CMD_RELOAD_FLASH   = 2,
    CMD_FACTORY_RESET  = 3,
    CMD_REBOOT_BOOTSEL = 4,
    CMD_SELECT_BLOCK   = 5,   // arg = block index for next GET CONFIG_BLOCK
    CMD_APPLY          = 6,   // re-init pins/ADC from current RAM config
};

typedef struct {
    uint8_t  command;
    uint8_t  _reserved[3];
    uint32_t arg;            // command-specific
} hid_command_t;

// --- STATUS -------------------------------------------------------------
typedef struct {
    uint16_t protocol_version;
    uint16_t config_version;
    uint16_t fw_version;          // major<<8 | minor
    uint8_t  flags;               // bit0: last_save_ok, bit1: defaults_in_use
    uint8_t  _reserved;
    uint8_t  build[24];           // null-terminated build id (e.g. git short sha)
} hid_status_t;

#define HID_STATUS_FLAG_LAST_SAVE_OK   0x01
#define HID_STATUS_FLAG_DEFAULTS       0x02

// --- RAW_INPUT ----------------------------------------------------------
// Live raw values for the calibration UI. Buttons mirror the debounced state.
typedef struct {
    uint16_t axis_raw[JOY_AXIS_COUNT];   // ADC raw (0..4095) or 0 if unused
    uint16_t buttons;                    // bit per button
    uint8_t  hat;                        // same encoding as input report
    uint8_t  _reserved;
} hid_raw_input_t;
#pragma pack(pop)

#endif
