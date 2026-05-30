# ESP32-S3 LVGL Graphics, Memory, Bus Dossier

Date: 2026-05-30

## Executive Summary

This dossier treats the current firmware as the broken baseline. The visible failure is GIF corruption on first render and after sprite changes. A boot-time I2C `ESP_ERR_INVALID_STATE` has also been observed before `Beep played`, which places the first suspect in the early touch/audio control path rather than in Wi-Fi or OTA.

The strongest current graphics hypothesis is internal-memory pressure plus an unproven GIF color-format change. The current code allocates both LVGL display buffers from internal RAM first, then forces `lv_gif` to `LV_COLOR_FORMAT_ARGB8888`. The user log after this change shows largest internal blocks dropping from about 64 KiB to about 25 KiB while GIF loads continue. That is below a comfortable margin for LVGL object, GIF, filesystem, audio, and Wi-Fi allocations. This should be treated as a regression experiment, not a fix.

The next implementation pass should not stack more speculative rendering changes. First measure baseline, then test one variable at a time: display pipeline, GIF decoder, tile lifecycle, file source lifetime, allocator placement, and I2C/audio sequencing.

## Local Firmware Facts

| Area | Current repo state |
| --- | --- |
| Framework | PlatformIO, Arduino, `pioarduino/platform-espressif32`, `esp32-s3-devkitc1-n16r8` |
| LVGL | `lvgl/lvgl@^9.5.0`, `LV_COLOR_DEPTH 16`, `LV_USE_GIF 1`, custom allocator |
| Display | `Arduino_ESP32QSPI` + `Arduino_SH8601`, 368 x 448, QSPI pins `4/5/6/7/11/12` |
| LVGL display mode | Partial render, 40 lines, `LV_DISPLAY_ROTATION_90`, software rotation in flush callback |
| Current draw buffers | Two 44,160 byte buffers: draw buffer and rotation buffer, internal-RAM first |
| Touch/control I2C | `SDA=15`, `SCL=14`, shared FT3168 + ES8311, currently 100 kHz |
| Audio data | I2S `MCLK=16`, `BCK=9`, `DI=10`, `WS=45`, `DO=8`, PA `46` |
| Filesystem | LittleFS exposed to LVGL as drive `S`, sprites under `S:/sprites/*.gif` |
| UI architecture | `lv_tileview` with Hello, Pet, Wi-Fi pages; all tile containers are created at startup |
| Pet lifecycle | Current state lazily creates/deletes the GIF object based on active tile and forces ARGB8888 |

## Current Regression Snapshot

Observed after recent experiments:

- Boot still logs an I2C error before `Beep played`.
- Idle GIF is corrupted on first render.
- Manual sprite changes now also corrupt all GIFs.
- Internal heap collapses during sprite activity: example log shows `internal=34272 largest_internal=25076`.
- `Loading sprite idle` appears after boot even when the Hello tile is visible, because sprite selection and tile creation are decoupled from actual Pet tile visibility.

Debug run snapshot, 2026-05-30:

- Display init plus two 44,160 byte internal draw buffers reduced internal free heap from about 250 KiB to 156 KiB.
- Wi-Fi config reduced internal free heap further to about 81 KiB, with largest internal block about 72 KiB.
- First Pet activation loaded `idle.gif` with only about 74 KiB internal free and 64 KiB largest block available.
- Creating the GIF object dropped internal free heap to about 49 KiB; `lv_gif_set_src()` dropped it to about 36 KiB with largest internal block about 28 KiB.
- Repeated sprite changes and page swipes repeatedly converged on largest internal block around 25 KiB while the GIF was active.
- No `esp32-hal-i2c-ng` error appeared in the captured terminal buffer, so the remaining immediately actionable issue is memory pressure/GIF corruption.

Working interpretation:

- `lv_tileview` does not automatically prevent hidden tile object logic from existing. Hidden tiles are still LVGL objects unless the app explicitly lazy-loads or pauses/deletes expensive children.
- Lazy loading is a valid strategy, but deleting/recreating GIF objects from tile-change/event paths must be tested against `lv_async_call()` or a pending main-loop action before accepting it.
- ARGB8888 is LVGL GIF's default and safest for transparency/disposal semantics, but it costs `width * height * 4` bytes plus decoder state. For the 53 x 35 idle GIF this is small, but the current allocator and internal draw-buffer choice make internal RAM unnecessarily tight.

## Official LVGL Findings

Sources:

- [LVGL Tileview documentation](https://docs.lvgl.io/master/details/widgets/tileview.html)
- [LVGL GIF documentation](https://docs.lvgl.io/master/details/widgets/gif.html)
- [LVGL display setup](https://docs.lvgl.io/master/details/main-modules/display/setup.html)
- [LVGL display rotation](https://docs.lvgl.io/master/details/main-modules/display/rotation.html)
- [LVGL memory documentation](https://docs.lvgl.io/master/details/main-modules/memory.html)
- [LVGL `lv_display.h`](https://github.com/lvgl/lvgl/blob/master/src/display/lv_display.h)
- [LVGL `lv_gif.c`](https://github.com/lvgl/lvgl/blob/master/src/widgets/gif/lv_gif.c)

Key implications for this firmware:

- Tileview is a scrollable container with child tiles. It manages the active tile and scrolling, not page virtualization. Expensive page logic must be app-managed.
- LVGL display flush receives the rendered area and a raw pixel map for that exact area. The driver must copy only the requested rectangle, honor width/height/stride, and call `lv_display_flush_ready()` exactly when safe.
- `lv_display_set_rotation()` swaps logical display resolution internally. Software rotation requires rotating both the area and pixel buffer before sending to a non-rotated panel driver.
- For RGB565 byte order, LVGL documents `lv_draw_sw_rgb565_swap()` in the flush path if the panel expects the opposite byte order. This needs a direct-driver color-pattern test before changing.
- `lv_gif_set_color_format()` supports `RGB565`, `RGB565_SWAPPED`, `RGB888`, `XRGB8888`, and `ARGB8888`; upstream constructor defaults to `ARGB8888`.
- `lv_gif_set_src()` closes the previous GIF, opens the new one, creates a draw buffer of the GIF canvas size/color format, clears it, sets that draw buffer as the image source, starts the timer, and immediately draws the next frame.
- `lv_gif_restart()` explicitly clears the draw buffer and resets decoder state. That makes restart a useful controlled experiment after first load and after source swaps.
- LVGL custom allocation is global. A policy that routes many medium allocations to internal RAM competes with Wi-Fi, I2C/I2S, Arduino, filesystem, and display buffers.

## ESP32-S3 And Board Findings

Sources:

- [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 technical reference manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [Espressif heap capabilities](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/mem_alloc.html)
- [Espressif SPI LCD docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd.html)
- [Waveshare ESP32-S3-Touch-AMOLED-1.8 wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8)
- [Waveshare ESP32-AIChats](https://github.com/waveshareteam/ESP32-AIChats)
- [Waveshare SH8601 component](https://github.com/waveshareteam/ESP32-AIChats)
- [Makerfabs MaTouch exact/near board examples](https://github.com/Makerfabs/MaTouch-ESP32-S3-AMOLED-with-Touch-1.8-FT3168)

Board-level implications:

- Internal SRAM is the scarce resource. PSRAM is large but has latency/cache constraints and not every allocation can safely live there.
- DMA-capable display buffers commonly use internal/DMA memory in ESP-IDF examples, but Arduino_GFX's `draw16bitRGBBitmap()` path is a synchronous CPU copy. That means PSRAM draw buffers may be acceptable if they preserve internal SRAM, but direct measurement is required.
- The 368 x 448 RGB565 frame is about 322 KiB. A 40-line partial buffer is 29,440 pixels or 58,880 bytes for the logical display if unrotated by 368 x 40; current code's allocation is 44,160 bytes because it uses `LCD_WIDTH * 40 * 2`.
- Software rotation requires an additional rotation buffer of equal partial-area capacity. This doubles display-buffer pressure before any GIF allocation.
- The SH8601 QSPI bus is independent of I2C and I2S, so GIF corruption is unlikely to be caused directly by FT3168/ES8311 traffic. Shared CPU/cache/memory pressure can still affect timing.
- FT3168 and ES8311 share I2C. Startup beep before full UI means ES8311 control transactions happen early and are a plausible source of boot-time `ESP_ERR_INVALID_STATE`.
- USB CDC logging is useful but not free. High-frequency flush/GIF frame logs must be rate-limited to avoid creating a timing artifact.

## Bus And Channel Map

| Bus/channel | Pins | Owners | Init/order risk | Debug action |
| --- | --- | --- | --- | --- |
| Display QSPI | `CS=12`, `SCLK=11`, `D0=4`, `D1=5`, `D2=6`, `D3=7` | Arduino_GFX SH8601 and LVGL flush | Rotation/color/stride mismatch can produce pixel soup | Direct pattern test before GIF tests |
| Touch/control I2C | `SDA=15`, `SCL=14`, FT3168 + ES8311 | `Wire`, touch driver, codec driver | Boot error before beep points at I2C init or ES8311 register writes | Probe addresses before/after touch/audio init; log register address on failure |
| Audio I2S | `MCLK=16`, `BCK=9`, `DI=10`, `WS=45`, `DO=8` | ES8311 codec data path | I2S startup should occur after codec control state is valid | Test startup beep disabled and codec init at 100/400 kHz |
| PA enable | `46` | Audio | Pop/beep sequencing can interact with codec init | Gate PA until codec config succeeds |
| USB CDC | Native USB | Serial monitor/OTA logs | Excessive logging can perturb timing | Add compile-time debug flags and rate limits |
| LittleFS | Flash | Sprite storage + LVGL FS driver | File-backed decoder can expose seek/read bugs | Hash/read-check before `lv_gif_set_src()`; test compiled-in GIF |

## Official And Vendor Comparison Corpus

| Source | Stack | Relevant pattern | Risk/lesson |
| --- | --- | --- | --- |
| Waveshare wiki/examples | Arduino, LVGL, Arduino_GFX, exact SH8601/FT3168/ES8311 family | Vendor confirms pin map and expected peripheral combination | Prefer board-specific init sequences over generic examples |
| Makerfabs MaTouch repo | Arduino, Arduino_GFX, FT3168, SH8601-like AMOLED | Exact-size 368 x 448 examples use direct images and LVGL demos | Good display/touch sanity reference, not a GIF lifecycle reference |
| Waveshare ESP32-AIChats | ESP-IDF, esp_lcd SH8601, LVGL, ES8311, exact board target | Board-specific `esp32-s3-touch-amoled-1.8` exists; separates board, display, audio | Strong reference for init order and bus ownership |
| LVGL upstream | LVGL 9.5 docs/code | `lv_gif` alloc/clear/open behavior; display flush/rotation contracts | Local code should match contracts before app-level debugging |
| Arduino_GFX | Arduino display path | SH8601/QSPI support and RGB565 drawing primitives | Direct-driver tests should use Arduino_GFX first |
| Espressif `esp-bsp` | ESP-IDF BSPs | DMA callbacks, esp_lcd flush-ready, display task discipline | Useful for robust architecture if Arduino path becomes limiting |
| Espressif I2S/ES8311 examples | ESP-IDF | ES8311 and I2S sequencing | Use as reference for codec reset/config delays |

## Independent Apps And Patterns

Candidate repository corpus. Metadata was collected on 2026-05-30 with GitHub CLI where available.

| Repo | Stars/forks | Last activity | Domain | Board/stack | Why it matters |
| --- | ---: | --- | --- | --- | --- |
| [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) | 26874/5950 | 2026-05-30 | AI agent/status | ESP-IDF, LVGL, many ESP32 boards, exact Waveshare AMOLED target | Best popular AI-agent reference; exact-board implementation exists |
| [andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock](https://github.com/andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock) | 10/1 | 2026-05-22 | Pet/animation/clock | Arduino_GFX, LVGL 9.5, SD GIFs | Strong GIF memory/lifecycle reference |
| [0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face) | 14/3 | 2026-05-25 | Pet/status animation | LVGL 9 widget, RGB565 canvas, heap caps | Avoids GIF; uses deterministic LVGL drawing and timers |
| [firsttris/esphome-energy-dashboard](https://github.com/firsttris/esphome-energy-dashboard) | 11/3 | 2026-05-20 | Dashboard | ESPHome LVGL, Guition ESP32-S3 display | Good declarative dashboard + touch architecture |
| [UsefulElectronics/esp32s3_lilygo_8bit_parallel_display_lvgl](https://github.com/UsefulElectronics/esp32s3_lilygo_8bit_parallel_display_lvgl) | 70/15 | 2026-04-17 | Demo/dashboard | ESP-IDF, LVGL, S3 parallel display | Buffer and tileview reference |
| [espzav/Multiple-LCD-Demo](https://github.com/espzav/Multiple-LCD-Demo) | 14/7 | 2026-05-25 | Game/multi-display | ESP-IDF, LVGL, multiple buses | Shows flush-ready callback discipline and app screens |
| [iamfaraz/Waveshare_ST7262_LVGL](https://github.com/iamfaraz/Waveshare_ST7262_LVGL) | 38/15 | 2026-03-25 | Dashboard/library | Arduino, Waveshare ESP32-S3 LCD, LVGL | Similar Waveshare S3 touch/display integration |
| [BruceDevices/firmware](https://github.com/BruceDevices/firmware) | 5755/1937 | 2026-05-30 | Popular ESP32 app firmware | Arduino/PlatformIO, many displays | Popular standalone app; useful display abstraction practices |
| [DrNeuroSurg/TINYRadio9](https://github.com/DrNeuroSurg/TINYRadio9) | 10/7 | 2026-04-12 | Radio/audio UI | WT32-SC01-Plus, LVGL-style UI | Similar audio + display concurrency domain |
| [jgauchia/ESPCompuTone](https://github.com/jgauchia/ESPCompuTone) | 12/3 | 2024-12-31 | Audio recorder | ESP32 audio + UI | Audio/display bus sequencing comparison |
| [MakersFunDuck/CrowPanel-esp32S3-5.79-inch-eink-display-LVGL-9-port](https://github.com/MakersFunDuck/CrowPanel-esp32S3-5.79-inch-eink-display-LVGL-9-port) | 6/1 | 2026-02-12 | LVGL port | ESP32-S3, LVGL 9, PlatformIO | LVGL 9 porting discipline on constrained screen |
| [anlopo/lilygo_t-dysplay-s3_platformio](https://github.com/anlopo/lilygo_t-dysplay-s3_platformio) | 3/0 | 2025-08-26 | S3 display app | ESP-IDF, PlatformIO, LilyGo T-Display-S3 | Similar S3 display memory constraints |
| [medmes/Go-Display-S3](https://github.com/medmes/Go-Display-S3) | 0/0 | 2025-01-06 | Scaffold | LilyGo T-Display-S3 | Small app scaffold reference |
| [dobodu/Lilygo_Waveshare_Amoled_Micropython](https://github.com/dobodu/Lilygo_Waveshare_Amoled_Micropython) | 11/4 | 2026-05-23 | AMOLED driver | MicroPython, LilyGo/Waveshare AMOLED | Non-LVGL SH8601/AMOLED reference |
| [lvgl/lv_port_esp32](https://github.com/lvgl/lv_port_esp32) | 1284/478 | 2026-05-30 | Port reference | ESP32, LVGL | Popular reference, official-adjacent not personal |

### Deep Analyses

#### 78/xiaozhi-esp32

- Exact-board support exists under `main/boards/waveshare/esp32-s3-touch-amoled-1.8`.
- Uses ESP-IDF `esp_lcd` SH8601 QSPI path instead of Arduino_GFX.
- Has a display abstraction and lock guard around LVGL mutations; app logic schedules display updates instead of writing from arbitrary tasks.
- For emotions it uses image descriptors and a custom GIF controller, not the upstream `lv_gif` widget. The custom decoder allocates about `5 * width * height` plus optional cache and initializes alpha deliberately.
- Strong pattern to copy: keep one persistent `lv_image` for emotion display; swap its source under lock; stop/reset controller before replacing it.
- Relevance: high. This is the closest popular exact-board application and suggests our app should avoid recreating LVGL widgets as the primary animation state machine.

#### andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock

- Uses Arduino_GFX + LVGL 9.5 and file-backed GIFs with an LVGL FS drive `S:/`.
- The README explicitly constrains GIFs to 160 x 86 because LVGL GIF requires a contiguous ARGB8888 canvas; at 320 x 172 that is about 220 KiB.
- Uses a custom SD filesystem bridge and calls `lv_gif_set_src()` against stable paths.
- GIF overlay is a top-level modal; lifecycle is explicit and timed. It swaps GIF sources in place for emotion changes instead of deleting/recreating the whole view.
- Uses `loop()` with `lv_timer_handler()` and avoids blocking work in the main loop.
- Relevance: high for GIF lifetime, file-backed GIFs, and memory budgeting.

#### 0015/lvgl_kawaii_face

- Does not use GIF; renders a pet/status face from LVGL draw buffers and timers.
- Allocates internal RAM first, then PSRAM fallback for non-DMA, but uses DMA-capable allocation where needed.
- Uses RGB565 canvas-like buffers and controlled LVGL timers instead of decoder/file IO.
- Strong pattern: if GIF remains fragile, a sprite-sheet or procedural face widget can give deterministic memory and frame timing.
- Relevance: high as a fallback architecture for pet animation.

#### UsefulElectronics/esp32s3_lilygo_8bit_parallel_display_lvgl

- ESP-IDF LVGL example uses a single DMA draw buffer and comments that a draw buffer should be at least about 1/10 screen size.
- Tileview demo eagerly creates several tiles but content is mostly labels/images/timers, not decoders with large transient memory.
- Flush-ready is signaled from the LCD IO transfer callback, not immediately after queueing.
- Relevance: medium-high for tileview and display-buffer discipline.

#### espzav/Multiple-LCD-Demo

- Multi-display ESP-IDF app with SPI, parallel, and I2C LCD paths.
- Saves flush-ready callback per display backend and returns readiness only after the relevant LCD transaction completes.
- Uses `lv_obj_clean()` and screen redraw functions to switch app views, rather than keeping every complex screen actively updating.
- Includes a 2048 game and dashboard-style screens.
- Relevance: medium-high for app architecture and flush callback separation.

#### firsttris/esphome-energy-dashboard

- ESPHome declarative LVGL dashboard for Guition ESP32-S3 display.
- Separates hardware bus config, display pages, touch, and UI layout YAML.
- Uses multiple pages/views but relies on ESPHome/LVGL's structured page model rather than manual cross-module object mutation.
- Relevance: medium for page architecture and avoiding hidden-page work.

#### Makerfabs/MaTouch-ESP32-S3-AMOLED-with-Touch-1.8-FT3168

- Exact/near hardware reference: 368 x 448 AMOLED, FT3168, Arduino_GFX, vendor demos.
- Examples include direct 16-bit image drawing and LVGL demos, good for validating color order and geometry.
- Not a strong GIF lifecycle reference.
- Relevance: high for display/touch sanity checks, low for GIF behavior.

#### Waveshare ESP32-AIChats

- Vendor app package includes prebuilt exact-board firmware zip variants for ESP32-S3-Touch-AMOLED-1.8 and other Waveshare displays.
- Useful as a binary/reference behavior source if source package versions diverge.
- Relevance: high for proving the hardware can run display + ES8311 + touch + AI UI, but lower for direct code transfer.

### Repeated Successful Patterns

- Separate board drivers from UI logic.
- Keep LVGL mutations on one LVGL/task context or behind a lock/scheduler.
- Use persistent display widgets and swap image data/source when possible.
- Treat GIF memory as a first-class budget, especially contiguous canvas memory.
- Avoid doing filesystem, Wi-Fi, audio startup, and GIF decoding all at the same moment without measurement.
- Validate display color/stride/rotation with deterministic test patterns before debugging image decoders.
- Prefer explicit page activation events for expensive logic; do not assume hidden tiles stop timers or allocations.

### Red Flags / Anti-Patterns

- Forcing ARGB8888 while also moving display buffers into internal SRAM without measuring largest free internal block.
- Deleting and recreating `lv_gif` during tile-change or sprite-change events before testing `lv_async_call()`/main-loop deferral.
- Letting hidden pages own active decoders/timers by default.
- Calling display flush ready before asynchronous DMA completion. Current Arduino_GFX path appears synchronous, but if migrated to esp_lcd this must change.
- Logging every frame/flush over USB CDC during performance tests.
- Treating a single successful manual sprite change as proof.

## Controlled Experiment Matrix

Each experiment must be one firmware variant. Record: git diff summary, build flags, boot log, heap snapshots, visible display behavior, cold boot result, 10 sprite changes, 10 page swipes.

| ID | Experiment | One variable | Acceptance |
| --- | --- | --- | --- |
| E0 | Broken baseline instrumentation | Add logs only | Reproduce current corruption and boot I2C log with memory/tile/GIF state captured |
| E1 | Direct Arduino_GFX color pattern | Bypass LVGL | Red/green/blue/white/black geometry and byte order are correct |
| E2 | LVGL color pattern | No GIF | LVGL flush output matches direct driver for color, clipping, rotation |
| E3 | GIF single-screen RGB565 | Minimal one-GIF app, RGB565 | Known GIF loads cold and survives 10 restarts |
| E4 | GIF single-screen RGB565 swapped | Only color format changed | Determine whether byte order affects GIF but not patterns |
| E5 | GIF single-screen ARGB8888 | Only color format changed | Determine correctness vs memory cost |
| E6 | File-backed vs compiled-in GIF | Only source type changed | If compiled works and LittleFS fails, focus FS/read/seek/cache |
| E7 | Persistent GIF widget | No delete/recreate; use `lv_gif_set_src()` + `lv_gif_restart()` | 10 source swaps without corruption |
| E8 | Event-deferred lifecycle | Use `lv_async_call()` or main-loop pending action | Page swipes never corrupt or leak |
| E9 | Tileview eager vs lazy | Only Pet tile content creation timing | Hidden Hello tile does not cause GIF decode |
| E10 | PSRAM display buffers | Restore display buffers to PSRAM-first or explicit PSRAM | Largest internal block stays above threshold |
| E11 | Internal display buffers | Internal-only display buffers | Quantify FPS/heap tradeoff; likely reject if largest block drops below 64 KiB |
| E12 | Allocator threshold sweep | Change LVGL internal-first threshold only | Find threshold that avoids starving internal SRAM |
| E13 | Disable startup beep | Audio control path disabled | Boot I2C error disappears or remains |
| E14 | Disable touch init | Touch path disabled | Isolate FT3168 vs ES8311 |
| E15 | I2C probe sequence | Address scan before/after each init | Identify exact missing/invalid device state moment |
| E16 | ES8311 register tracing | Log register address/status | Identify failing transaction or prove benign |

## Instrumentation To Add

Compile-time flags:

- `APP_DEBUG_GRAPHICS=1`: tile changes, GIF create/delete/src/restart/frame counts, LVGL mem monitor, heap snapshots.
- `APP_DEBUG_FLUSH=1`: first N flush rectangles, color format, stride, rotation, optional CRC/sample pixels.
- `APP_DEBUG_I2C=1`: I2C init state, address probes, ES8311 register write/read failures with register addresses, recovery events.
- `APP_EXPERIMENT_DISPLAY_PATTERN=1`: direct and LVGL color/geometry patterns.
- `APP_EXPERIMENT_SINGLE_GIF=1`: minimal screen with no tileview/Wi-Fi dashboard interaction.
- `APP_EXPERIMENT_DISABLE_BEEP=1` and `APP_EXPERIMENT_DISABLE_TOUCH=1`: bus isolation.

Initial implementation status:

- `include/debug/debug_log.h` now defines disabled-by-default debug helpers for heap/LVGL memory and I2C checkpoints.
- `platformio.ini` now includes `esp32-s3-devkitc-1-debug`, enabling `APP_DEBUG_GRAPHICS`, `APP_DEBUG_I2C`, and `APP_DEBUG_FLUSH`.
- `src/main.cpp`, `src/ui/ui.cpp`, `src/wifi_config/wifi_config.cpp`, and `src/i2c_bus/i2c_bus.cpp` now emit rate-limited or lifecycle-scoped debug logs only when the debug env/flags are enabled.
- These changes are instrumentation only; they do not change GIF color format, tile lifecycle, display buffers, or I2C timing.

Rate limiting:

- Flush logs: first 20 flushes and then once per 5 seconds.
- GIF frame logs: source load/restart and every 30th frame only.
- Heap logs: before/after major lifecycle events.

## Current Hypotheses

| Hypothesis | Probability | Evidence | First test |
| --- | ---: | --- | --- |
| Internal SRAM starvation causes current all-GIF corruption | High | Largest internal block near 25 KiB after ARGB/internal-buffer change | E10 vs E11 |
| GIF color format mismatch or transparency/disposal issue caused original first-render corruption | Medium-high | Manual Show originally fixed first load; ARGB change did not fix | E3-E5 |
| File-backed LittleFS read/seek/source lifetime issue | Medium | GIF path is file-backed and loaded after dashboard events | E6 |
| Tileview hidden lifecycle triggers GIF at wrong time | Medium | `Loading idle` occurs while Hello tile visible; tileview does not virtualize | E7-E9 |
| Display rotation/stride bug corrupts only image-like content | Medium | Software rotation path is custom | E1-E2 |
| Boot I2C error is ES8311 init/register write | Medium-high | Timestamp appears before `Beep played` | E13, E16 |
| Boot I2C error is FT3168 touch polling | Medium | Touch initialized before beep | E14-E15 |

## Recommended Architecture If Experiments Confirm

Target design:

- Page view creates lightweight tiles at startup.
- Pet page creates one persistent `lv_image` or `lv_gif` child on first activation.
- Hidden Pet page pauses GIF/timer but does not delete/recreate objects during scroll events.
- Sprite changes use stable source storage, `lv_gif_set_src()`, then `lv_gif_restart()` if needed.
- Lifecycle changes requested from events are deferred to main LVGL loop via `lv_async_call()` or a pending action processed before/after `lv_timer_handler()`.
- Display pipeline remains RGB565 with a verified byte-order policy and deterministic pattern tests.
- Draw/rotation buffers should preserve internal SRAM unless measured FPS requires internal. On Arduino_GFX synchronous copy, PSRAM buffers may be the better stability choice.
- GIF canvas color format should be chosen by correctness first, then memory: test RGB565, RGB565 swapped, ARGB8888. Do not force ARGB8888 globally without internal-block thresholds.
- I2C bus must have one owner module, idempotent init, address probes, and register-level ES8311 error logs.

## Immediate Next Steps

1. Run E3 with `LV_COLOR_FORMAT_RGB565` while keeping PSRAM-first display buffers.
2. If RGB565 is still corrupted, run E4 with `LV_COLOR_FORMAT_RGB565_SWAPPED`.
3. If both RGB565 modes fail, run E1/E2 display-pattern tests before any further GIF lifecycle work.
4. Run E6 with file-backed vs compiled-in GIF once the display color path is proven.
5. If direct/LVGL patterns pass, restore a memory-safe GIF baseline: PSRAM display buffers, persistent GIF object, no event-time deletion, and the first proven GIF color format.
6. Run I2C isolation separately from graphics so audio/touch changes do not pollute GIF conclusions.

## Experiment Log

- 2026-05-30 15:10 CEST, E10 PSRAM-first display buffers: build succeeded and user flashed the debug firmware. Internal heap headroom improved substantially during GIF load, with largest internal blocks around 108-116 KiB instead of about 25 KiB. Visual result remained unchanged: all GIFs rendered as random pixels and did not animate. Conclusion: internal SRAM pressure was a real regression risk, but not the primary cause of the current GIF corruption.
- 2026-05-30 15:10 CEST, E3 RGB565 GIF canvas: changed `lv_gif_set_color_format()` from `LV_COLOR_FORMAT_ARGB8888` to `LV_COLOR_FORMAT_RGB565` while keeping PSRAM-first display buffers. User result: same random-pixel, non-animated GIFs. Conclusion: ARGB8888 blending was not the only cause.
- 2026-05-30 15:14 CEST, E4 RGB565 swapped GIF canvas: changed `lv_gif_set_color_format()` from `LV_COLOR_FORMAT_RGB565` to `LV_COLOR_FORMAT_RGB565_SWAPPED` while keeping PSRAM-first display buffers. This directly tests RGB565 byte order, a common issue in ESP32 display ports using 16-bit color.
- 2026-05-30 15:22 CEST, regression analysis of `d06e8cd`: logs show GIF file parsing succeeds (`idle=19`, `cat=3`, `parrot=9` frames), LVGL/display flushes continue, and heap is healthy with PSRAM display buffers. The largest risky regression in `d06e8cd` is the switch from one persistent GIF widget to lazy create/delete/recreate on tile changes and sprite changes, plus the ARGB8888 format change. Restored persistent GIF widget creation, kept hidden-tile queuing, changed tile exit to pause/hide instead of delete, changed source swaps to `lv_gif_set_src()` + `lv_gif_restart()` + `lv_gif_resume()`, and restored RGB565 GIF canvas.

## Acceptance Criteria

- Cold boot shows no corrupted sprite pixels when first entering the Pet tile.
- Sprite changes between `idle`, `cat`, and `parrot` render correctly for at least 10 changes.
- At least 10 page swipes do not corrupt GIFs or leak memory.
- Serial logs contain no unexplained I2C errors during boot or idle operation.
- Dossier and experiment table record the chosen tile lifecycle, GIF format, display byte order/rotation, draw-buffer placement, and I2C/audio/touch sequencing.
