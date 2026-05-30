---
name: codex-pet-gifs
description: Use when locating Codex Desktop built-in pet WebP spritesheets in app.asar, extracting pets such as codex, hoots, rocky, seedy, stacky, bsod, dewey, fireball, or null-signal, and converting their 8x9 state atlases into one animated GIF per state.
---

# Codex Pet GIFs

Built-in Codex Desktop pets are packaged in the Electron ASAR archive, not in the custom pet folder.

Custom pets live under:

```text
${CODEX_HOME:-$HOME/.codex}/pets/<pet-name>/
```

Built-in pets live inside:

```text
/Applications/Codex.app/Contents/Resources/app.asar
```

Their internal paths look like:

```text
/webview/assets/<pet>-spritesheet-v4-<hash>.webp
```

Known built-in pet ids include `bsod`, `codex`, `dewey`, `fireball`, `hoots`, `null-signal`, `rocky`, `seedy`, and `stacky`.

## Generate GIFs

Use the bundled script. Prefer the Codex bundled Python runtime when available because it includes Pillow:

```sh
/Users/thomasdouche/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
  .agents/skills/codex-pet-gifs/scripts/extract_pet_gifs.py \
  --pet codex \
  --output-dir /private/tmp/codex-pet-gifs \
  --duration-ms 500
```

The script extracts the WebP atlas and writes one GIF per state:

```text
<pet>-idle.gif
<pet>-running-right.gif
<pet>-running-left.gif
<pet>-waving.gif
<pet>-jumping.gif
<pet>-failed.gif
<pet>-waiting.gif
<pet>-thinking.gif
<pet>-review.gif
```

The app's internal `running` state is emitted as `thinking`, because that row represents active work / processing in the UI.

## Atlas Contract

Built-in and custom pet sheets use the Codex pet atlas contract:

```text
1536x1872 image
8 columns x 9 rows
192x208 cell size
transparent unused cells
```

State row order:

```text
idle
running-right
running-left
waving
jumping
failed
waiting
running/thinking
review
```

The script drops fully transparent frames by default, which removes unused cells at the end of shorter animations.
