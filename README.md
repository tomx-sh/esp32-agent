# ESP32 Agent

Monorepo for an ESP32-S3 AI agent device and its host companion. The firmware provides an LVGL display UI, BLE connectivity, ES8311 audio, and web-based Wi-Fi configuration. The companion connects desktop AI agents to the device.

```text
firmware/   PlatformIO and Arduino firmware
companion/  Host companion and integration hooks
```

## OTA flashing

Run firmware commands from the PlatformIO project directory:

```sh
cd firmware
```

The first flash has to be done over USB:

```sh
pio run -t upload
```

After the device is running this firmware and connected to Wi-Fi, upload OTA with:

```sh
pio run -e esp32-s3-devkitc-1-ota -t upload
```

The OTA target uses `esp32-agent.local:3232`. If mDNS does not resolve on your network, replace `upload_port` in `firmware/platformio.ini` with the IP shown on the device display.

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
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" codex-waving 4000",
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
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" codex-thinking 0",
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
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" codex-waiting 0",
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
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" codex-review 3000",
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
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" codex-idle 0",
            "statusMessage": "Showing ESP32 agent idle"
          }
        ]
      }
    ]
  }
}
```

Example `companion/scripts/esp32-agent-hook.sh`:

```sh
#!/bin/sh
set -eu

sprite="${1:-codex-idle}"
ttl_ms="${2:-0}"
base_url="${ESP32_AGENT_URL:-http://esp32-agent.local}"

payload=$(printf '{"name":"%s","ttlMs":%s}' "$sprite" "$ttl_ms")

curl -fsS -m 2 -X POST "$base_url/pet" \
  -H 'Content-Type: application/json' \
  -d "$payload" >/dev/null
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
curl -X POST "$ESP32_AGENT_URL/codex/usage" -H 'Content-Type: application/json' -d '{"percent":42,"resetAt":1786147200,"resetCredits":2}'
curl -X POST "$ESP32_AGENT_URL/codex/message" -H 'Content-Type: application/json' -d '{"message":"Building firmware","muted":true}'
curl -X POST "$ESP32_AGENT_URL/codex/context" -H 'Content-Type: application/json' -d '{"remainingPercent":68}'
```

`resetAt` is required and uses the same Unix-seconds timestamp shape as Codex. Send `0` when no reset is available. The device synchronizes a UTC clock over SNTP and renders resets under 24 hours as a countdown; longer resets use an abbreviated UTC calendar date.

`POST /codex/message` sets the one-line message overlaid at the top of the Codex Usage page. Normal messages appear above the GIF with a drop shadow. Set `muted` to `true` to use the same muted gray as the usage gauge, disable the shadow, and place the text behind the GIF; when omitted, the current muted state is preserved. Messages may contain up to 240 bytes; line breaks are rejected, text wider than the display is truncated with a single ellipsis glyph, and an empty string clears the message. `GET /codex/message` returns the current message, muted state, and maximum length. The values are held in memory and reset when the device restarts.

The message label uses LVGL's built-in 36 px Montserrat font. The numeric and reset labels use a generated 36 px JetBrains Mono font containing only their curated glyph set; it also supplies the single ellipsis glyph that Montserrat lacks as a fallback for truncated messages. The generated bitmap remains uncompressed for fast rendering and is rebuilt with `firmware/scripts/generate_fonts.sh`. Regenerate it after replacing the source TTF:

```sh
cd firmware
./scripts/generate_fonts.sh
```

`remainingPercent` is the active thread's remaining context capacity, not an account usage limit. Codex App Server emits `thread/tokenUsage/updated` with `tokenUsage.last.totalTokens` and `tokenUsage.modelContextWindow`. Codex calculates the displayed percentage after reserving a 12,000-token baseline:

```text
effectiveWindow = modelContextWindow - 12000
used = max(last.totalTokens - 12000, 0)
remainingPercent = round(100 * max(effectiveWindow - used, 0) / effectiveWindow)
```

## Claude Code event hooks

Claude Code can drive the same display API, but its hook configuration is settings-based instead of `hooks.json`-based.

Claude Code hook reference: <https://code.claude.com/docs/en/hooks>
Claude Code settings reference: <https://code.claude.com/docs/en/settings>

Claude Code loads hooks from `~/.claude/settings.json`, `.claude/settings.json`, or `.claude/settings.local.json`. For this repo, use `.claude/settings.json` for team-shared hooks or `.claude/settings.local.json` for personal overrides. Settings reload automatically when they change, and `/hooks` shows the active hooks. `matcher` only applies to `PreToolUse` and `PostToolUse`, so leave it out for the other events.

Example `.claude/settings.json`:

```json
{
  "hooks": {
    "SessionStart": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" claude-code-session-start 4000"
          }
        ]
      }
    ],
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" claude-code-user-prompt-submit 0"
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "Bash|Edit|Write",
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" claude-code-pre-tool-use 0"
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "Bash|Edit|Write",
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" claude-code-post-tool-use 3000"
          }
        ]
      }
    ],
    "Notification": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" claude-code-notification 0"
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "ESP32_AGENT_URL=http://esp32-agent.local sh \"$(git rev-parse --show-toplevel)/companion/scripts/esp32-agent-hook.sh\" claude-code-stop 0"
          }
        ]
      }
    ]
  }
}
```

Useful Claude events:

| Claude event | Suggested sprite | Notes |
| --- | --- | --- |
| `SessionStart` | `claude-code-session-start` | New or resumed session |
| `UserPromptSubmit` | `claude-code-user-prompt-submit` | Prompt accepted |
| `Notification` | `claude-code-notification` | Claude is waiting on input or permission |
| `PreToolUse` | `claude-code-pre-tool-use` | Tool is about to run; can be noisy |
| `PostToolUse` | `claude-code-post-tool-use` | Tool completed |
| `Stop` | `claude-code-stop` | Turn finished |

If `esp32-agent.local` does not resolve, set `ESP32_AGENT_URL` to the IP shown on the device display, for example `ESP32_AGENT_URL=http://192.168.1.42`.
