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

## Codex event hooks

The firmware exposes a small HTTP API that can show the bundled pet GIFs on the display. Codex hooks can call that API so the device reacts to Codex lifecycle events.

Codex hook reference: <https://developers.openai.com/codex/hooks>

Codex looks for hooks in `hooks.json` or inline `[hooks]` tables next to active config layers. For this repo, use a project-local file at `.codex/hooks.json`; for all projects, use `~/.codex/hooks.json` or `~/.codex/config.toml`. Command hooks receive the hook event JSON on `stdin`, run with the Codex session `cwd`, and must be reviewed/trusted with `/hooks` before Codex runs them.

Example `.codex/hooks.json`:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "matcher": "startup|resume",
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/.codex/hooks/esp32-agent.sh\" codex-waving 4000",
            "statusMessage": "Showing ESP32 agent session start"
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/.codex/hooks/esp32-agent.sh\" codex-thinking 0",
            "statusMessage": "Showing ESP32 agent thinking"
          }
        ]
      }
    ],
    "PermissionRequest": [
      {
        "matcher": "Bash|apply_patch",
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/.codex/hooks/esp32-agent.sh\" codex-waiting 0",
            "statusMessage": "Showing ESP32 agent waiting"
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "Bash|apply_patch",
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/.codex/hooks/esp32-agent.sh\" codex-review 3000",
            "statusMessage": "Showing ESP32 agent tool review"
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/.codex/hooks/esp32-agent.sh\" codex-idle 0",
            "statusMessage": "Showing ESP32 agent idle"
          }
        ]
      }
    ]
  }
}
```

Example `.codex/hooks/esp32-agent.sh`:

```sh
#!/bin/sh
set -eu

sprite="${1:-codex-idle}"
ttl_ms="${2:-0}"
base_url="${ESP32_AGENT_URL:-http://esp32-agent.local}"

curl -fsS -m 2 -X POST "$base_url/pet" \
  -H 'Content-Type: application/json' \
  -d "{\"name\":\"$sprite\",\"ttlMs\":$ttl_ms}" >/dev/null
```

If `esp32-agent.local` does not resolve, set `ESP32_AGENT_URL` to the IP shown on the device display, for example `ESP32_AGENT_URL=http://192.168.1.42`.

Useful event-to-sprite mapping:

| Codex event | Suggested sprite | Notes |
| --- | --- | --- |
| `SessionStart` | `codex-waving` | New or resumed session |
| `UserPromptSubmit` | `codex-thinking` | User prompt accepted |
| `PermissionRequest` | `codex-waiting` | Codex is blocked on approval |
| `PreToolUse` | `codex-running-right` | Tool is about to run; can be noisy |
| `PostToolUse` | `codex-review` | Tool completed |
| `Stop` | `codex-idle` | Turn finished |

Available bundled sprites:

- `idle`
- `codex-idle`
- `codex-thinking`
- `codex-waiting`
- `codex-review`
- `codex-waving`
- `codex-jumping`
- `codex-running-left`
- `codex-running-right`
- `codex-failed`

Manual curl commands:

```sh
export ESP32_AGENT_URL=http://esp32-agent.local

curl -X POST "$ESP32_AGENT_URL/pet" -H 'Content-Type: application/json' -d '{"name":"codex-thinking","ttlMs":5000}'
curl -X POST "$ESP32_AGENT_URL/pet" -H 'Content-Type: application/json' -d '{"name":"codex-waiting","ttlMs":0}'
curl -X POST "$ESP32_AGENT_URL/pet" -H 'Content-Type: application/json' -d '{"name":"codex-review","ttlMs":5000}'
curl -X POST "$ESP32_AGENT_URL/pet" -H 'Content-Type: application/json' -d '{"name":"codex-failed","ttlMs":5000}'
curl -X POST "$ESP32_AGENT_URL/pet" -H 'Content-Type: application/json' -d '{"name":"codex-idle","ttlMs":0}'
curl -X POST "$ESP32_AGENT_URL/pet/message" -H 'Content-Type: application/json' -d '{"message":"Codex needs approval","ttlMs":5000}'
```
