"""Sanity check for the rp2040joy HID feature-report protocol.

Reads STATUS, dumps RAW_INPUT for a few seconds, then reads back the live
config via the chunked CONFIG_BLOCK protocol and prints a summary.
"""
import struct
import sys
import time

import hid

VID, PID = 0xCAFE, 0x4004

REPORT_ID_CONFIG_BLOCK = 0x10
REPORT_ID_COMMAND      = 0x11
REPORT_ID_STATUS       = 0x12
REPORT_ID_RAW_INPUT    = 0x13

CMD_SELECT_BLOCK = 5

CONFIG_BLOCK_PAYLOAD = 56
JOY_CONFIG_SIZE = 132  # 4 + 6*10 + 16*2 + 8 (hat) + 8 (mux) + 8 (sr) + 8 (gearbox) + 4 (crc)
JOY_AXIS_COUNT = 6
JOY_BUTTON_COUNT = 16


def open_device():
    d = hid.device()
    d.open(VID, PID)
    print(f"opened: manufacturer={d.get_manufacturer_string()!r} "
          f"product={d.get_product_string()!r} serial={d.get_serial_number_string()!r}")
    return d


def read_status(d):
    # hidapi: get_feature_report(report_id, length). length includes the leading id byte.
    raw = bytes(d.get_feature_report(REPORT_ID_STATUS, 33))
    print(f"STATUS raw ({len(raw)}B): {raw.hex()}")
    rid = raw[0]
    proto, cfg_v, fw, flags, _r = struct.unpack_from("<HHHBB", raw, 1)
    build = bytes(raw[1+8:1+8+24]).split(b"\x00", 1)[0].decode("ascii", "replace")
    print(f"  report_id={rid:#x} proto={proto} config_v={cfg_v} fw={fw:#06x} flags={flags:#x} build={build!r}")


def read_raw_input(d, count=20, interval=0.05):
    print("RAW_INPUT:")
    for _ in range(count):
        raw = bytes(d.get_feature_report(REPORT_ID_RAW_INPUT, 17))
        axes = struct.unpack_from(f"<{JOY_AXIS_COUNT}H", raw, 1)
        buttons, hat = struct.unpack_from("<HB", raw, 1 + JOY_AXIS_COUNT * 2)
        print(f"  axes={axes} buttons={buttons:016b} hat={hat}")
        time.sleep(interval)


def select_block(d, idx):
    # hid.Device.send_feature_report wants bytes including the leading report id.
    pkt = [REPORT_ID_COMMAND, CMD_SELECT_BLOCK, 0, 0, 0,
           idx & 0xFF, (idx >> 8) & 0xFF, (idx >> 16) & 0xFF, (idx >> 24) & 0xFF]
    d.send_feature_report(pkt)


def read_config_block(d, idx):
    select_block(d, idx)
    raw = bytes(d.get_feature_report(REPORT_ID_CONFIG_BLOCK, 1 + 2 + CONFIG_BLOCK_PAYLOAD))
    block_idx, block_total = raw[1], raw[2]
    payload = bytes(raw[3:3 + CONFIG_BLOCK_PAYLOAD])
    return block_idx, block_total, payload


def read_full_config(d):
    blocks = []
    total_blocks = (JOY_CONFIG_SIZE + CONFIG_BLOCK_PAYLOAD - 1) // CONFIG_BLOCK_PAYLOAD
    for i in range(total_blocks):
        idx, total, payload = read_config_block(d, i)
        if total != total_blocks:
            print(f"  WARN: device says total={total}, host expected {total_blocks}")
        if idx != i:
            print(f"  WARN: requested block {i}, device returned {idx}")
        blocks.append(payload)
    cfg = b"".join(blocks)[:JOY_CONFIG_SIZE]
    return cfg


def parse_config(cfg):
    print(f"CONFIG ({len(cfg)}B):")
    version, poll_hz = struct.unpack_from("<HH", cfg, 0)
    print(f"  version={version} poll_hz={poll_hz}")

    # axes start at offset 4: 6 × 10-byte axis_cfg_t
    off = 4
    for i in range(JOY_AXIS_COUNT):
        src, flags, rmin, rcen, rmax, dz = struct.unpack_from("<BBHHHH", cfg, off)
        off += 10
        en = "ON " if flags & 0x01 else "off"
        inv = " inv" if flags & 0x02 else ""
        print(f"  axis[{i}] {en}{inv} src={src} min={rmin} cen={rcen} max={rmax} dz={dz}")

    # buttons: 16 × 2 bytes
    for i in range(JOY_BUTTON_COUNT):
        gpio, flags = struct.unpack_from("<BB", cfg, off)
        off += 2
        if flags & 0x01:
            al = " active_low" if flags & 0x02 else " active_high"
            print(f"  btn[{i:2d}] gpio={gpio}{al}")

    # hat: 8 bytes
    up, right, down, left, hflags = struct.unpack_from("<BBBBB", cfg, off)
    off += 8
    en = "ON" if hflags & 0x01 else "off"
    print(f"  hat {en} u={up} r={right} d={down} l={left}")

    print(f"  (parse stopped at offset {off}, total {len(cfg)})")


def main():
    d = open_device()
    try:
        read_status(d)
        cfg = read_full_config(d)
        parse_config(cfg)
        read_raw_input(d, count=10, interval=0.1)
    finally:
        d.close()


if __name__ == "__main__":
    sys.exit(main())
