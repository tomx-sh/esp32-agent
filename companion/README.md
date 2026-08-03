# ESP32 Agent Companion

`esp32-agent` connects Codex Desktop activity and account usage to the ESP32 Agent display.

## Install

Requirements:

- Go 1.25.8 or newer
- The `codex` CLI installed and signed in
- The ESP32 Agent on the same network

```sh
go install ./cmd/esp32-agent
```

Go installs the executable in `$(go env GOPATH)/bin`. Add that directory to your
shell `PATH` once; for zsh:

```sh
echo 'export PATH="$HOME/go/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

Then verify the installation:

```sh
esp32-agent --version
```

## Guided mode

Running the tool without a command opens a styled interactive guide for the primary operations:

```sh
esp32-agent
```

Use the arrow keys and Enter to choose an operation; Ctrl-C exits. The first
option runs the complete first-time setup: it saves the settings, tests the
device, and asks before installing global Codex hooks. Settings-only
configuration, one-off usage sync, pet synchronization, the bridge, hooks, and
status remain available as separate operations. The guide automatically falls
back to simple numbered prompts when a terminal UI is unavailable or
`ACCESSIBLE` is set.

## Commands

```text
esp32-agent setup           Run the guided first-time setup
esp32-agent configure       Change settings without modifying Codex hooks
esp32-agent device test     Verify connectivity and display current device values
esp32-agent sync            Sync quota and context once
esp32-agent pet sync        Convert and upload the selected Codex Desktop pet
esp32-agent pet status      Compare the Desktop selection with the device pet
esp32-agent run             Run the foreground bridge until interrupted
esp32-agent status          Show configuration and live device state
esp32-agent hooks install   Merge global lifecycle hooks into ~/.codex/hooks.json
esp32-agent hooks uninstall Remove only ESP32 Agent hook handlers
esp32-agent device pet      Show a bundled sprite
esp32-agent device message set TEXT  Set the generic Codex message
esp32-agent device message clear     Clear the generic Codex message
esp32-agent completion      Generate shell completion instructions
```

Every command supports `--help`. Use `--config PATH` to select another configuration file and `--verbose` to show hook delivery errors.

## First-time setup

Run the complete guided flow:

```sh
esp32-agent setup
```

Setup configures the companion, tests the device, and then explicitly asks
whether to add the Codex lifecycle commands to the displayed hooks file. It
ends with a summary of every path and change.

To change settings later without modifying hooks, run:

```sh
esp32-agent configure
```

Or set values using flags:

```sh
esp32-agent configure \
  --device-url http://esp32-agent.local \
  --poll-interval 5m \
  --codex codex \
  --context on
```

The equivalent manual flow is:

```sh
esp32-agent device test
esp32-agent hooks install
esp32-agent run
```

## Synchronize the Codex Desktop pet

Flash the current firmware once, then run:

```sh
esp32-agent pet sync
```

The companion reads `desktop.selected-avatar-id` through Codex App Server. For
custom pets it reads the local manifest under `$CODEX_HOME/pets`; for built-in
pets on macOS it reads the selected WebP sprite sheet from the installed
ChatGPT or Codex application without modifying the app. Both v1 (8×9) and v2
(8×11) sprite sheets are supported.

Conversion happens on the computer. The companion creates nine transparent,
192×208 GIFs with Codex Desktop's real frame timings, uploads them under
versioned names, and activates the complete pack only after every GIF succeeds.
The ESP32 continues to use its general-purpose GIF player. Run
`esp32-agent pet status` to see whether the selected Desktop pet and device are
in sync.

Open **Settings > Hooks** in Codex Desktop after installation and trust the
ESP32 Agent definitions. Existing hooks and unknown fields in
`~/.codex/hooks.json` are preserved. The handlers observe lifecycle events only;
they do not approve, deny, block, or rewrite Codex actions.

The installer configures ten of Codex's eleven current lifecycle events.
`PreToolUse` is intentionally omitted because `UserPromptSubmit` already puts
the device in its working state; running another command before every tool
would add latency without showing new information. Hook delivery is bounded to
one second so an unavailable device cannot hold Codex for the normal HTTP
timeout.

## Data sources

- Quota remaining, reset time, and reset-credit count come from `account/rateLimits/read` on a short-lived `codex app-server` process. The companion converts Codex's raw used percentage at this adapter boundary so every downstream component uses the same remaining-percentage meaning.
- GIF state and generic messages come from session, prompt, tool-result, approval, compaction, subagent, and stop hooks. Hooks emit pet-independent semantic states (`running`, `waiting`, `failed`, `review`, and `idle`), which the active synchronized pet pack maps to GIF files. Attention states such as approval requests and detected tool failures use normal messages; routine progress messages are muted. A tool is marked failed only when its structured result exposes `isError`, `success: false`, or a non-zero exit code.
- Context percentage comes from the most recent `token_count` record in the active transcript path supplied by Codex hooks.

Context parsing is deliberately isolated. If Codex changes its internal JSONL format, the companion reports context as unavailable and does not overwrite the device with a false zero value. Quota and lifecycle features continue working independently.

## Local files

On macOS, configuration is stored under `~/Library/Application Support/esp32-agent/config.json` and runtime state under `~/Library/Caches/esp32-agent/state.json`. Other operating systems use Go's standard user config and cache locations.

Neither file contains Codex credentials. Authentication remains owned by the installed Codex CLI.

## Development

```sh
go test ./...
go vet ./...
go build ./cmd/esp32-agent
```
