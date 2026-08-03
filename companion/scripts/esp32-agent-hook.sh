#!/bin/sh
set -eu

sprite="${1:-codex-idle}"
ttl_ms="${2:-0}"
base_url="${ESP32_AGENT_URL:-http://esp32-agent.local}"

payload=$(printf '{"name":"%s","ttlMs":%s}' "$sprite" "$ttl_ms")

curl -fsS -m 2 -X POST "$base_url/pet" \
  -H 'Content-Type: application/json' \
  -d "$payload" >/dev/null
