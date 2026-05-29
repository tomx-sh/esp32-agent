---
name: esp32-usb-flash
description: Use when flashing this ESP32 Agent firmware over USB with PlatformIO/esptool, selecting the correct ESP32-S3 serial port, or troubleshooting USB upload failures such as "Failed to connect to ESP32-S3", "Serial data stream stopped", missing USB-Serial/JTAG devices, bad cables, or bootloader-mode issues.
---

# ESP32 USB Flash

Use USB flashing for the first install, recovery from broken firmware, or when OTA is unavailable.
Prefer fixing the physical port/cable/bootloader path before changing firmware code.

## Identify The Correct Port

Scan serial devices:

```sh
ls /dev/cu.*
pio device list
```

Use the ESP32-S3 USB-Serial/JTAG interface for flashing:

```text
USB VID:PID=303A:1001
Description: USB JTAG/serial debug unit
```

Example flashable ports seen for this project:

```text
/dev/cu.usbmodem101
/dev/cu.usbmodem215301
```

Do not choose the running firmware's composite USB interface for esptool flashing:

```text
USB VID:PID=0483:0011
Description: BIGMEUNT VCPHID
```

That interface can appear as `/dev/cu.usbmodem1C69A3AC00003` and has failed with:

```text
Failed to connect to ESP32-S3: Serial data stream stopped: Possible serial noise or corruption.
```

## Flash Workflow

Build and upload to the explicit USB-Serial/JTAG port:

```sh
pio run -e esp32-s3-devkitc-1 -t upload --upload-port /dev/cu.usbmodemXXXX
```

Successful connection starts like:

```text
Connected to ESP32-S3
USB mode: USB-Serial/JTAG
Uploading stub flasher...
```

Successful completion includes hash verification and `SUCCESS`.

## Troubleshooting

- If only `0483:0011 BIGMEUNT VCPHID` appears, the board is visible but not through the flashable interface. Try a known data-capable USB cable, a different USB port, or put the board into bootloader/download mode before rescanning.
- If changing the cable makes a `303A:1001 USB JTAG/serial debug unit` device appear, use that new `/dev/cu.usbmodem...` path explicitly.
- If upload fails before `Connected to ESP32-S3`, rescan with `pio device list`; macOS can assign a new port path after unplug/replug or reset.
- If a process may be holding the port, check it with `lsof /dev/cu.usbmodemXXXX`.
- If PlatformIO prints a `.platformio/.cache` ownership warning after `pio device list`, treat it as a local PlatformIO cache issue, not the esptool connection failure, unless it prevents the actual build/upload command from running.
- If the board does not enumerate as `303A:1001` with a good cable, manually enter ESP32-S3 download mode, then rescan and upload again.

## Notes

- This project has `ARDUINO_USB_MODE=1` and `ARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini`; those flags affect firmware USB behavior but do not make the `BIGMEUNT VCPHID` runtime port a reliable esptool flashing target.
- After this firmware is flashed once and connected to Wi-Fi, prefer the `esp32-ota-flash` skill for OTA updates.
