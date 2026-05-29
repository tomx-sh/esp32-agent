# ESP32 Agent

Voice-enabled AI agent device for the ESP32-S3. Features an LVGL display UI, BLE connectivity, ES8311 audio with an agent backend, and a web-based WiFi configuration interface. Built with PlatformIO and Arduino framework.

## OTA flashing

The first flash still has to be done over USB:

```sh
pio run -t upload
```

After the device is running this firmware and connected to Wi-Fi, upload OTA with:

```sh
pio run -e esp32-s3-devkitc-1-ota -t upload
```

The OTA target uses `esp32-agent.local:3232`. If mDNS does not resolve on your network, replace `upload_port` in `platformio.ini` with the IP shown on the device display.
