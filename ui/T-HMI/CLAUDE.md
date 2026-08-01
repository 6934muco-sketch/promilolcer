# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for an ESP32-based breathalyzer ("alkolmetre") with an LVGL/TFT_eSPI touchscreen UI, an
addressable LED status ring, and voice-prompt audio (DFPlayer Mini). Written as an Arduino sketch
(`ui/ui.ino`) plus two small satellite modules (`led.cpp/h`, `ses.cpp/h`). Comments and Serial log
strings throughout are in Turkish.

## Repository layout

- `ui/ui.ino` — main sketch: watchdog setup, sensor reading/compensation, the core measurement
  state machine, and `setup()`/`loop()`. This is the file to read first; everything else hangs off it.
- `ui/led.cpp` / `ui/led.h` — WS2812/NeoPixel status-ring driver, run from its own ~30ms LVGL timer
  independent of the 100ms sensor timer for smooth animation.
- `ui/ses.cpp` / `ui/ses.h` — DFPlayer Mini voice-prompt queue (non-blocking; serviced from `loop()`).
- `ui/*_DAVRANIS_NOTU.md` — behavior spec/design notes per subsystem (sensor state machine, LED,
  sound). Read the relevant one before changing that subsystem's logic — they document *why*, not
  just what.
- `ui/SERIAL_LOG_HARITASI.md` — an index of every `Serial.print*` call with file:line links and which
  `#if FLAG` toggles gate it. **Keep this file in sync whenever you add, remove, or move a
  `Serial.*` call** (line numbers and the table both need updating).
- `ui/SES.pdf`, `ui/MQ3.pdf` — hardware/sensor reference material.
- `libraries/` — vendored third-party Arduino libraries (TFT_eSPI, lvgl, Adafruit BMP280/BusIO/
  NeoPixel/Unified_Sensor, DHT_sensor_library, DFRobotDFPlayerMini). Treat as read-only vendor code
  unless a fix genuinely belongs there; `libraries/lv_conf.h` is the project's LVGL config.
- **Not in this repo**: `ui.h` and the SquareLine-Studio-generated screen/widget code
Location: `C:\Users\kayna\OneDrive\Desktop\alkolmeterLCD\ui\T-HMI\libraries\ui` and generated screen files.
  (`ui_ekran1`..`ui_ekran11`, `uic_*` objects, `_ui_screen_change`, `ui_init`) that `ui.ino` includes
  and calls. These are generated externally and must be present alongside the sketch to compile —
  don't try to "find" their definitions in this repo.

## Build

This is an Arduino sketch, not a CMake/PlatformIO project — build via the Arduino IDE or
`arduino-cli` targeting an ESP32 board, with `libraries/` on the library search path (or symlinked
into the Arduino libraries folder) and the missing SquareLine `ui.h`/generated UI sources present
next to `ui.ino`. There is no test suite, linter, or CI in this repo — validate changes by reading
the state machine carefully and, where possible, by flashing to hardware and watching Serial output
(115200 baud) via `ui/SERIAL_LOG_HARITASI.md`'s flag toggles.

## Architecture

### Two independent LVGL timers

- **`alkolmetre_timer_cb`** (100ms, `SENSOR_TIMER_MS`): reads sensors, runs the measurement state
  machine, drives the screen (`uic_*` widgets), triggers audio prompts.
- **`ledServis_cb`** (30ms): drives LED animation only, decoupled from the sensor timer so
  animations (breathing, fill, pulse) stay smooth. `ledDurumGuncelle()` (called every sensor tick)
  maps the current `Durum` to a `LedDurum` via `ledDurumAyarla()`, which is idempotent — safe to
  call every tick.

Both run inside `loop()`'s `lv_timer_handler()`. A hardware Task Watchdog (`esp_task_wdt`) wraps
`setup()`+`loop()` (they share the same FreeRTOS task), fed via `wdtBesle()`; timeout is longer during
`setup()` (10s, covers known long waits) and short during normal operation (4s, so a real hang is
caught fast). Feed the watchdog inside any new long-running/blocking loop you add.

### Core measurement state machine (`Durum` enum, `alkolmetre_timer_cb`)

`BEKLE` (idle) → `TABAN_OLC` (baseline re-measured fresh before every measurement) → `OLCUM`
(sensor-wait phase, then a 6s analysis window once a breath is detected) → `SONUC` (result shown,
10s cooldown, fan runs) → `KANAL_TEMIZLE` (fixed 10s fan purge) → back to `BEKLE`. A separate manual
path: holding the button 10s in `BEKLE` enters `R0_KALIBRASYON` (PURGE → SETTLE → ORNEKLE phases,
recalibrates the MQ-3 clean-air resistance `R0`, persisted to NVS), with `R0_KAL_BASARISIZ_MESAJ` as
an error-display state if it's cancelled by contamination.

Key invariants worth knowing before touching this:
- The alcohol reading is a **peak-hold** value (`olcumPeakPromil`), gated by `GEREKLI_ARDISIK_SAYI`
  consecutive above-threshold readings — never read the live promil value as the final result.
- `analizYaziGosterildi` gates when peak accumulation actually starts (not just when raw MQ-3 output
  crosses the threshold) — removing this guard was a past bug (see the comment above that block in
  `ui.ino`) that let noise leak into the result before the "Analiz yapılıyor" prompt ever showed.
- Contamination detection has a primary path (BME280 breath-channel pressure spike vs. its own
  slow EMA baseline) and an automatic fallback (MQ-3's own raw ADC reading) used only when the
  BME280 is considered faulty (`nefesSensorArizali`, set on I2C-not-found, "frozen reading" for 10
  minutes, or a physically-impossible pressure). `kanalKirliMiEfektif` is the value the state
  machine actually consults — always use it, not `kanalKirliMi`/`mq3KirliMi` directly.
- The fallback contamination counter must never run during `OLCUM` (a real breath legitimately spikes
  MQ-3 output — that's not contamination); the timer callback already guards this.
- MQ-3 raw reading uses temperature/humidity compensation against a datasheet-digitized lookup table
  (`nemSicaklikKatsayisi`), bilinearly interpolated. `R0` is persisted in NVS in a `/60` scaled form —
  see the comment on `mg_L`'s coefficient in `hamPromilOlcVeDondur()` if you touch that scaling.

### Two temperature/humidity/pressure sensors, different jobs

- **Nefes-BME280** (I2C, GPIO21/22, `bmeNefes`) — near the breath channel; detects channel
  contamination via pressure-spike-vs-own-EMA, and (when healthy) triggers the OLCUM sensor-wait→
  analysis transition.
- **DHT22 ortam** (GPIO32, `dhtOrtam`) — next to the MQ-3; feeds temperature/humidity into the MQ-3
  compensation formula only. No longer does contamination detection (that moved to BME280/MQ-3
  fallback).

GPIO4 is unusable (conflicts with TFT_RST) — this is why the breath sensor moved to I2C rather than
a second single-wire DHT.

### Persistence (NVS, `Preferences`, namespace `"alkolmetre"`)

Stores `R0`, calibration reference temp/humidity, and a measurement counter. Loaded once at boot
(`r0veReferansiYukle`), written only on successful manual R0 calibration and after each completed
measurement (counter only). `wdtBesle()` is called immediately before/after NVS writes since flash
erase/write can be slow enough to trip the tightened runtime watchdog.

### LED subsystem (`led.cpp`)

Table-driven: `LedDurum` → `LedProfil` (color + animation + brightness) via a parallel array indexed
by the enum, so adding a new `LedDurum` requires updating both `led.h`'s enum and `PROFILLER` in
lockstep. Animations are all `millis()`-based and non-blocking. `UFLEME_DOLMA_SURESI_MS` in
`led.cpp` must be kept manually in sync with `ANALIZ_OLCUM_SURESI_MS` in `ui.ino` (documented
in-code; there's no shared constant).

### Sound subsystem (`ses.cpp`)

Non-blocking queue serviced from `loop()`. Two SD card folders: `01` for general prompts (played via
`sesCal(fileNo)`), `02` for measurement results — `001`-`051` are spoken numbers `0.00`-`0.50` ‰
(`sesPromilSayiCal`, file = `round(promil*100)+1`, silent above 0.50), `052`-`054` are the
normal/ticari-sınır/yasal-sınır status announcements (`sesPromilDurumCal`).

## Conventions

- Turkish is used throughout for identifiers' comments, log strings, and the behavior-note docs;
  match that when editing existing code (new public identifiers already follow a
  `kucukHarfBirlesikTurkce` naming style — keep it consistent within a file).
- Tunable thresholds/timings are `static const` at file scope in `ui.ino`, usually with a comment
  explaining the reasoning/units — prefer adding new tunables the same way over hardcoding magic
  numbers inline.
