---
name: esp32-ota-flash
description: Use when flashing this ESP32 Agent firmware over Wi-Fi with PlatformIO OTA, validating the build, or diagnosing basic OTA upload failures for this repository.
---

# ESP32 OTA Flash

Use OTA only after the device already runs firmware that includes `ArduinoOTA`.
The first OTA-capable firmware must be flashed over USB.

## Preconditions

- The ESP32 is powered on and connected to the same Wi-Fi network as the development machine.
- The device is running firmware with `ota_update_init()` called after Wi-Fi setup.
- The main loop calls `ota_update_loop()`.
- PlatformIO has the `esp32-s3-devkitc-1-ota` environment configured with `upload_protocol = espota`.

## Workflow

Enter the PlatformIO project directory:

```sh
cd firmware
```

Build the OTA environment first:

```sh
pio run -e esp32-s3-devkitc-1-ota
```

Upload OTA:

```sh
pio run -e esp32-s3-devkitc-1-ota -t upload
```

Successful upload ends with PlatformIO reporting `Result attempt 1: 'OK'` and `SUCCESS`.

## Target

Default OTA target:

```ini
upload_port = esp32-agent.local
upload_flags =
  --port=3232
```

If `.local` hostname resolution fails, replace `upload_port` with the IP shown on the device display.

## Troubleshooting

- If OTA cannot connect, confirm the ESP32 is connected to station Wi-Fi, not only serving its setup access point.
- If upload hangs before transfer, check that the computer and ESP32 are on the same network/VLAN.
- If OTA is not available after a fresh device flash, flash once over USB with `pio run -t upload`.
