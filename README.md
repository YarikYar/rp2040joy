# rp2040joy

A configurable USB HID joystick on the RP2040, with a Qt6 desktop
configurator. Bare pico-sdk firmware — no Arduino, no MicroPython.

## Features

- Up to **6 analog axes** (16-bit), **16 buttons**, **1 HAT switch**
- Per-axis calibration: min / center / max + symmetric deadzone + invert
- Optional **4051-class analog multiplexer** for axes beyond the 4 native ADC channels
- Optional chained **74HC165 shift registers** for buttons (up to 32 bits on 3 GPIOs)
- Optional **gearbox / H-pattern shifter** post-processor
  - **Hold** — natural mutex, only the engaged gear is reported
  - **Pulse** — short pulse on each gear engagement instead of a held state
  - **Sequential** — gear switches collapse into virtual *Shift Up* / *Shift Down* buttons
- Configuration stored in flash, edited live over USB via HID Feature Reports
- Configurator with live preview, axis calibration wizard, and BOOTSEL reboot

## Layout

```
firmware/  C firmware (pico-sdk + TinyUSB)
app/       Qt6 configuration tool (uses hidapi)
tools/     Python HID diagnostics + udev rules
```

## Build

Firmware (requires `arm-none-eabi-gcc` and pico-sdk):

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S firmware -B firmware/build
cmake --build firmware/build -j
# → firmware/build/rp2040joy.uf2
```

Hold BOOTSEL on the RP2040 while plugging it in, then drag-and-drop
the `.uf2` onto the `RPI-RP2` mass-storage drive that appears.

Qt configurator (requires Qt6 Widgets + hidapi-hidraw):

```sh
cmake -S app -B app/build
cmake --build app/build -j
# → app/build/rp2040joy_app
```

## Linux permissions

Copy `tools/99-rp2040joy.rules` to `/etc/udev/rules.d/` and reload udev
so the HID device is accessible without root:

```sh
sudo cp tools/99-rp2040joy.rules /etc/udev/rules.d/
sudo udevadm control --reload
# replug the device
```

## Configuration protocol

Config lives in the last 4 KB sector of flash, framed with a magic
word and CRC-32. Host ↔ device exchange uses HID Feature Reports —
see `firmware/include/hid_protocol.h` for the wire format. The `tools/`
directory has small Python scripts that exercise it end-to-end.
