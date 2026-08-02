#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

lvgl_converter=$(find .pio/libdeps -path '*/lvgl/scripts/LVGLImage.py' -print -quit)
if [ -z "$lvgl_converter" ]; then
  echo "LVGLImage.py not found; run 'pio run' once to install project dependencies." >&2
  exit 1
fi

temporary_dir=$(mktemp -d)
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM

mkdir -p src/ui/icons

npx --yes sharp-cli@5.2.0 \
  -i assets/icons/lucide/flask-round.svg \
  -o "$temporary_dir/{name}.png" \
  -f png \
  resize 24 24

uv run \
  --with 'pypng==0.20220715.0' \
  --with 'lz4==4.4.5' \
  python "$lvgl_converter" \
  --ofmt C \
  --cf A8 \
  --compress NONE \
  --output src/ui/icons \
  --name flask_round_24 \
  "$temporary_dir/flask-round.png"

# PlatformIO exposes LVGL as <lvgl.h>; normalize the converter's fallback include.
generated_icon=src/ui/icons/flask_round_24.c
normalized_icon="$temporary_dir/flask_round_24.c"
sed 's|#include "lvgl/lvgl.h"|#include <lvgl.h>|' "$generated_icon" > "$normalized_icon"
mv "$normalized_icon" "$generated_icon"
