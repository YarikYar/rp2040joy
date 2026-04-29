"""Round-trip test: read → modify → write → save to flash → re-read.

Verifies that config writes via chunked CONFIG_BLOCK and CMD_SAVE_TO_FLASH
persist correctly.
"""
import struct
import sys
import time

import hid

VID, PID = 0xCAFE, 0x4004

REPORT_ID_CONFIG_BLOCK = 0x10
REPORT_ID_COMMAND      = 0x11
REPORT_ID_STATUS       = 0x12

CMD_SAVE_TO_FLASH  = 1
CMD_RELOAD_FLASH   = 2
CMD_FACTORY_RESET  = 3
CMD_SELECT_BLOCK   = 5

CONFIG_BLOCK_PAYLOAD = 56
JOY_CONFIG_SIZE = 132
TOTAL_BLOCKS = (JOY_CONFIG_SIZE + CONFIG_BLOCK_PAYLOAD - 1) // CONFIG_BLOCK_PAYLOAD


def open_device():
    d = hid.device()
    d.open(VID, PID)
    return d


def send_command(d, cmd, arg=0):
    pkt = [REPORT_ID_COMMAND, cmd, 0, 0, 0,
           arg & 0xFF, (arg >> 8) & 0xFF, (arg >> 16) & 0xFF, (arg >> 24) & 0xFF]
    d.send_feature_report(pkt)


def read_status(d):
    raw = bytes(d.get_feature_report(REPORT_ID_STATUS, 33))
    proto, cfg_v, fw, flags, _r = struct.unpack_from("<HHHBB", raw, 1)
    return {"proto": proto, "config_v": cfg_v, "fw": fw, "flags": flags}


def read_full_config(d):
    out = bytearray()
    for i in range(TOTAL_BLOCKS):
        send_command(d, CMD_SELECT_BLOCK, i)
        raw = bytes(d.get_feature_report(REPORT_ID_CONFIG_BLOCK, 1 + 2 + CONFIG_BLOCK_PAYLOAD))
        out.extend(raw[3:3 + CONFIG_BLOCK_PAYLOAD])
    return bytes(out[:JOY_CONFIG_SIZE])


def write_full_config(d, cfg_bytes):
    assert len(cfg_bytes) == JOY_CONFIG_SIZE
    padded = cfg_bytes + b"\x00" * (TOTAL_BLOCKS * CONFIG_BLOCK_PAYLOAD - JOY_CONFIG_SIZE)
    for i in range(TOTAL_BLOCKS):
        chunk = padded[i * CONFIG_BLOCK_PAYLOAD:(i + 1) * CONFIG_BLOCK_PAYLOAD]
        pkt = [REPORT_ID_CONFIG_BLOCK, i, TOTAL_BLOCKS] + list(chunk)
        d.send_feature_report(pkt)


# Layout helpers (must match config.h).
def get_poll_hz(cfg):       return struct.unpack_from("<H", cfg, 2)[0]
def set_poll_hz(cfg, v):    struct.pack_into("<H", cfg, 2, v)

# Axis 0 starts at offset 4 in joy_config_t.
# axis_cfg_t: src(1) flags(1) raw_min(2) raw_center(2) raw_max(2) deadzone(2)
# deadzone offset within axis = 1+1+2+2+2 = 8.
def get_axis0_dz(cfg):      return struct.unpack_from("<H", cfg, 4 + 8)[0]
def set_axis0_dz(cfg, v):   struct.pack_into("<H", cfg, 4 + 8, v)


def main():
    d = open_device()
    try:
        print("--- before ---")
        print(read_status(d))
        cfg0 = bytearray(read_full_config(d))
        print(f"poll_hz={get_poll_hz(cfg0)} axis0.deadzone={get_axis0_dz(cfg0)}")

        # Mutate.
        new_poll = 500
        new_dz   = 123
        cfg1 = bytearray(cfg0)
        set_poll_hz(cfg1, new_poll)
        set_axis0_dz(cfg1, new_dz)

        print(f"--- writing poll_hz={new_poll} axis0.deadzone={new_dz} ---")
        write_full_config(d, bytes(cfg1))

        # Read-back from RAM (no flash commit yet).
        cfg_ram = bytearray(read_full_config(d))
        print(f"RAM after write: poll_hz={get_poll_hz(cfg_ram)} axis0.deadzone={get_axis0_dz(cfg_ram)}")
        assert get_poll_hz(cfg_ram) == new_poll
        assert get_axis0_dz(cfg_ram) == new_dz

        # Commit + verify.
        print("--- CMD_SAVE_TO_FLASH ---")
        send_command(d, CMD_SAVE_TO_FLASH)
        time.sleep(0.2)  # firmware processes deferred save in main loop
        st = read_status(d)
        print(f"status after save: {st}")
        save_ok = bool(st["flags"] & 0x01)
        print(f"last_save_ok={save_ok}")
        assert save_ok, "device reported save failure"

        # Mutate RAM, then RELOAD — should restore the saved values.
        print("--- mutate RAM, then CMD_RELOAD_FLASH ---")
        cfg2 = bytearray(cfg_ram)
        set_poll_hz(cfg2, 999)
        write_full_config(d, bytes(cfg2))
        send_command(d, CMD_RELOAD_FLASH)
        time.sleep(0.1)
        cfg_after = bytearray(read_full_config(d))
        print(f"after reload: poll_hz={get_poll_hz(cfg_after)} axis0.deadzone={get_axis0_dz(cfg_after)}")
        assert get_poll_hz(cfg_after) == new_poll
        assert get_axis0_dz(cfg_after) == new_dz

        # Restore defaults in RAM, save, then read back.
        print("--- CMD_FACTORY_RESET + save ---")
        send_command(d, CMD_FACTORY_RESET)
        time.sleep(0.1)
        send_command(d, CMD_SAVE_TO_FLASH)
        time.sleep(0.2)
        cfg_def = bytearray(read_full_config(d))
        print(f"after reset+save: poll_hz={get_poll_hz(cfg_def)} axis0.deadzone={get_axis0_dz(cfg_def)}")
        assert get_poll_hz(cfg_def) == 1000
        assert get_axis0_dz(cfg_def) == 32

        print("\nALL CHECKS PASSED")
    finally:
        d.close()


if __name__ == "__main__":
    sys.exit(main())
