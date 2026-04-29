"""Round-trip the AXIS_FLAG_INVERT bit via CONFIG_BLOCK and verify storage."""
import struct
import time
import hid

VID, PID = 0xCAFE, 0x4004
CONFIG_BLOCK_PAYLOAD = 56
JOY_CONFIG_SIZE = 132
TOTAL_BLOCKS = (JOY_CONFIG_SIZE + CONFIG_BLOCK_PAYLOAD - 1) // CONFIG_BLOCK_PAYLOAD

REPORT_ID_CONFIG_BLOCK = 0x10
REPORT_ID_COMMAND      = 0x11
REPORT_ID_RAW_INPUT    = 0x13
CMD_SELECT_BLOCK = 5

# axis 0 starts at offset 4. flags is at offset 1 within axis_cfg_t.
AXIS0_FLAGS_OFFSET = 4 + 1
AXIS_FLAG_ENABLE = 0x01
AXIS_FLAG_INVERT = 0x02

d = hid.device(); d.open(VID, PID)

def cmd(c, a=0):
    d.send_feature_report([REPORT_ID_COMMAND, c, 0, 0, 0,
                           a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF])

def read_cfg():
    out = bytearray()
    for i in range(TOTAL_BLOCKS):
        cmd(CMD_SELECT_BLOCK, i)
        raw = bytes(d.get_feature_report(REPORT_ID_CONFIG_BLOCK, 1 + 2 + CONFIG_BLOCK_PAYLOAD))
        out.extend(raw[3:3 + CONFIG_BLOCK_PAYLOAD])
    return bytearray(out[:JOY_CONFIG_SIZE])

def write_cfg(buf):
    padded = bytes(buf) + b"\x00" * (TOTAL_BLOCKS * CONFIG_BLOCK_PAYLOAD - JOY_CONFIG_SIZE)
    for i in range(TOTAL_BLOCKS):
        chunk = padded[i*CONFIG_BLOCK_PAYLOAD:(i+1)*CONFIG_BLOCK_PAYLOAD]
        d.send_feature_report([REPORT_ID_CONFIG_BLOCK, i, TOTAL_BLOCKS] + list(chunk))

def axis0_flags(buf): return buf[AXIS0_FLAGS_OFFSET]

def raw_input():
    raw = bytes(d.get_feature_report(REPORT_ID_RAW_INPUT, 17))
    return struct.unpack_from("<H", raw, 1)[0]  # axis 0 raw

print("=== reading current config ===")
cfg = read_cfg()
print(f"axis0.flags = {axis0_flags(cfg):#04x}  (enable={'Y' if cfg[AXIS0_FLAGS_OFFSET]&1 else 'N'} invert={'Y' if cfg[AXIS0_FLAGS_OFFSET]&2 else 'N'})")

print("\n=== flipping AXIS_FLAG_INVERT for axis 0 ===")
cfg[AXIS0_FLAGS_OFFSET] ^= AXIS_FLAG_INVERT
print(f"new flags: {cfg[AXIS0_FLAGS_OFFSET]:#04x}")
write_cfg(cfg)
time.sleep(0.05)

cfg2 = read_cfg()
print(f"after write+read: axis0.flags = {axis0_flags(cfg2):#04x}")
assert axis0_flags(cfg) == axis0_flags(cfg2), "flag did not round-trip!"
print("flag round-trip OK")

print(f"\nlive raw axis0 = {raw_input()}  (raw is unaffected by invert)")
print("\nCheck the *scaled* gamepad value with: jstest /dev/input/jsX or evtest")
