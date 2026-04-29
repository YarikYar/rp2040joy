#include <string.h>
#include "usb_hid.h"
#include "tusb.h"
#include "pico/bootrom.h"
#include "config.h"
#include "flash_store.h"
#include "input.h"
#include "hid_protocol.h"

// Cross-loop signal: a command callback may request actions that should happen
// from the main loop, not from the USB IRQ context. (BOOTSEL reset is fine
// inline; flash save must run with USB idle, so we defer it.)
static volatile bool pending_save     = false;
static volatile bool pending_reload   = false;
static volatile bool pending_defaults = false;
static volatile bool pending_apply    = false;
static volatile bool last_save_ok     = true;

static uint8_t selected_block = 0;

// Build identifier (overridable from CMake later).
#ifndef RP2040JOY_BUILD_ID
#define RP2040JOY_BUILD_ID "dev"
#endif
#ifndef RP2040JOY_FW_VERSION
#define RP2040JOY_FW_VERSION 0x0001  // 0.1
#endif

void usb_hid_init(void) {
    tusb_init();
}

void usb_hid_task(void) {
    tud_task();

    // Drain deferred config operations. Order matters: defaults < reload < save.
    if (pending_defaults) {
        pending_defaults = false;
        config_set_defaults(config_get());
        input_reconfigure(config_get());
    }
    if (pending_reload) {
        pending_reload = false;
        flash_store_load(config_get());
        input_reconfigure(config_get());
    }
    if (pending_save) {
        pending_save = false;
        last_save_ok = flash_store_save(config_get());
        input_reconfigure(config_get());
    }
    if (pending_apply) {
        pending_apply = false;
        input_reconfigure(config_get());
    }
}

bool usb_hid_ready(void) {
    return tud_hid_ready();
}

void usb_hid_send(const gamepad_report_t *r) {
    tud_hid_report(HID_REPORT_ID_INPUT, r, sizeof(*r));
}

// --- Feature report handling -----------------------------------------------

static uint16_t handle_get_config_block(uint8_t *buffer, uint16_t reqlen) {
    hid_config_block_t block = {0};
    block.block_index = selected_block;
    block.block_total = HID_CONFIG_BLOCK_TOTAL;

    const uint8_t *src = (const uint8_t *)config_get();
    size_t total = sizeof(joy_config_t);
    size_t off   = (size_t)selected_block * HID_CONFIG_BLOCK_PAYLOAD;
    size_t len   = (off >= total) ? 0 :
                   (total - off > HID_CONFIG_BLOCK_PAYLOAD ? HID_CONFIG_BLOCK_PAYLOAD
                                                           : total - off);
    if (len) memcpy(block.payload, src + off, len);

    uint16_t out = sizeof(block);
    if (out > reqlen) out = reqlen;
    memcpy(buffer, &block, out);
    return out;
}

static uint16_t handle_get_status(uint8_t *buffer, uint16_t reqlen) {
    hid_status_t st = {0};
    st.protocol_version = HID_PROTOCOL_VERSION;
    st.config_version   = CONFIG_VERSION;
    st.fw_version       = RP2040JOY_FW_VERSION;
    st.flags = (last_save_ok ? HID_STATUS_FLAG_LAST_SAVE_OK : 0);
    const char *bid = RP2040JOY_BUILD_ID;
    size_t bidlen = strlen(bid);
    if (bidlen >= sizeof(st.build)) bidlen = sizeof(st.build) - 1;
    memcpy(st.build, bid, bidlen);

    uint16_t out = sizeof(st);
    if (out > reqlen) out = reqlen;
    memcpy(buffer, &st, out);
    return out;
}

static uint16_t handle_get_raw_input(uint8_t *buffer, uint16_t reqlen) {
    hid_raw_input_t raw = {0};
    for (uint8_t i = 0; i < JOY_AXIS_COUNT; i++) raw.axis_raw[i] = input_axis_raw(i);
    for (uint8_t i = 0; i < JOY_BUTTON_COUNT; i++)
        if (input_button(i)) raw.buttons |= (uint16_t)(1u << i);
    raw.hat = input_hat();

    uint16_t out = sizeof(raw);
    if (out > reqlen) out = reqlen;
    memcpy(buffer, &raw, out);
    return out;
}

static void handle_set_config_block(const uint8_t *buffer, uint16_t bufsize) {
    if (bufsize < sizeof(hid_config_block_t)) return;
    const hid_config_block_t *block = (const hid_config_block_t *)buffer;
    if (block->block_index >= HID_CONFIG_BLOCK_TOTAL) return;

    uint8_t *dst   = (uint8_t *)config_get();
    size_t   total = sizeof(joy_config_t);
    size_t   off   = (size_t)block->block_index * HID_CONFIG_BLOCK_PAYLOAD;
    if (off >= total) return;
    size_t len = (total - off > HID_CONFIG_BLOCK_PAYLOAD) ? HID_CONFIG_BLOCK_PAYLOAD
                                                          : total - off;
    memcpy(dst + off, block->payload, len);

    selected_block = block->block_index;
}

static void handle_set_command(const uint8_t *buffer, uint16_t bufsize) {
    if (bufsize < sizeof(hid_command_t)) return;
    const hid_command_t *cmd = (const hid_command_t *)buffer;
    switch (cmd->command) {
        case CMD_SAVE_TO_FLASH:
            pending_save = true;
            break;
        case CMD_RELOAD_FLASH:
            pending_reload = true;
            break;
        case CMD_FACTORY_RESET:
            pending_defaults = true;
            break;
        case CMD_REBOOT_BOOTSEL:
            // Disable mass storage (bit 0), keep PICOBOOT (bit 1) — but the
            // common pattern is to allow both. 0 = both enabled.
            reset_usb_boot(0, 0);
            break;
        case CMD_SELECT_BLOCK:
            if (cmd->arg < HID_CONFIG_BLOCK_TOTAL) selected_block = (uint8_t)cmd->arg;
            break;
        case CMD_APPLY:
            pending_apply = true;
            break;
        default: break;
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    if (report_type != HID_REPORT_TYPE_FEATURE) return 0;
    switch (report_id) {
        case HID_REPORT_ID_CONFIG_BLOCK: return handle_get_config_block(buffer, reqlen);
        case HID_REPORT_ID_STATUS:       return handle_get_status(buffer, reqlen);
        case HID_REPORT_ID_RAW_INPUT:    return handle_get_raw_input(buffer, reqlen);
        default: return 0;
    }
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    if (report_type != HID_REPORT_TYPE_FEATURE) return;
    switch (report_id) {
        case HID_REPORT_ID_CONFIG_BLOCK: handle_set_config_block(buffer, bufsize); break;
        case HID_REPORT_ID_COMMAND:      handle_set_command(buffer, bufsize); break;
        default: break;
    }
}
