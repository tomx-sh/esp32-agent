# ESP32 Agent

Monorepo for an ESP32-S3 AI agent display and its Codex Desktop companion.

```text
firmware/   PlatformIO and Arduino firmware
companion/  Go CLI and Codex integration
```

## Firmware

Run PlatformIO from the firmware project:

```sh
cd firmware
pio run
```

The first upload must use USB:

```sh
pio run -t upload
```

After the device is connected to Wi-Fi, OTA upload is available at `esp32-agent.local:3232`:

```sh
pio run -e esp32-s3-devkitc-1-ota -t upload
```

If mDNS does not resolve, replace `upload_port` in `firmware/platformio.ini` with the IP shown on the device.

## Codex Desktop companion

Install the CLI:

```sh
cd companion
go install ./cmd/esp32-agent
```

Run it without arguments for the guided interface:

```sh
esp32-agent
```

The normal first-time flow is:

```sh
esp32-agent configure
esp32-agent device test
esp32-agent hooks install
esp32-agent run
```

After installing hooks, open `/hooks` in Codex Desktop and trust the new definitions. The installer adds only `SessionStart`, `UserPromptSubmit`, `Stop`, and `SessionEnd`; it does not monitor tool calls or permission requests.

The foreground bridge polls Codex account limits through `codex app-server`, converts the reported usage to quota remaining, sends quota/reset information to the device, and follows the active transcript path supplied by hooks for context usage. The transcript parser is isolated and best-effort because Codex does not guarantee that file format; failures leave the last context value untouched.

See [companion/README.md](companion/README.md) for commands, configuration, and implementation details.

## Firmware assets

The generated web assets are rebuilt automatically by PlatformIO. To regenerate the curated JetBrains Mono font after replacing its source TTF:

```sh
cd firmware
./scripts/generate_fonts.sh
```

The firmware exposes the companion endpoints under `/pet`, `/codex/usage`, `/codex/message`, and `/codex/context`.
