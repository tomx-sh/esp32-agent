#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

npx --yes lv_font_conv@1.5.3 \
  --font assets/fonts/JetBrainsMono-Medium.ttf \
  --symbols '0123456789%dhm erstJanFebMarAprMayJunJulAugSepOctNovDec' \
  --size 36 \
  --bpp 4 \
  --format lvgl \
  --no-compress \
  --lv-include lvgl.h \
  --lv-font-name jetbrains_mono_36 \
  --output src/ui/fonts/jetbrains_mono_36.c
