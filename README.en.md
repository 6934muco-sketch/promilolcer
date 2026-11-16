# ESP32-Based Digital Breathalyzer

[Türkçe](README.md) | **English**

An ESP32-based device that measures breath alcohol concentration with an MQ-3 gas sensor and shows the result in per mille (‰) on a 4" TFT display. A WS2812 LED ring and voice prompts guide the user through each step.

The project combines analog sensor acquisition, temperature/humidity compensation, state-machine-driven embedded firmware, HMI design with LVGL, and reliability features: a watchdog, persistent calibration and sensor fault detection.

<!-- ![Device](docs/cihaz.jpg) -->
<!-- [Demo video](https://...) -->

---

## Highlights

- **State-machine measurement flow:** idle → baseline → breath detection → 6 s analysis → result → fan purge of the breath channel
- **Temperature/humidity compensation:** an Rs/R0 table digitized from the MQ-3 datasheet chart, read with **bilinear interpolation**
- **Dual-sensor channel contamination detection:** the primary method uses BMP280 pressure spikes. If that sensor is judged faulty, the firmware falls back automatically to the MQ-3's own reading.
- **Manual R0 calibration:** started by holding the button for 10 s. It runs a fan purge, settles, samples until stable and aborts if a breath is detected. The result is stored in the ESP32's **NVS** (persistent flash).
- **Hardware Task Watchdog (TWDT):** 10 s timeout during setup and 4 s at runtime. The reason for the previous reset is logged on every boot.
- **Multi-channel feedback:** an LVGL UI, a 15-LED WS2812 ring, and DFPlayer Mini voice prompts that also read the result aloud
- **Non-blocking architecture:** the sensor logic (100 ms) and the LED animation (30 ms) run on independent LVGL timers. The audio queue is serviced from `loop()`.

---

## Hardware

| Component | Role |
|---|---|
| ESP32 | Main controller |
| MQ-3 alcohol sensor module | Breath alcohol measurement (analog output, RL = 200 kΩ on module) |
| 4" IPS TFT, ST7796, 480×320, SPI | LVGL user interface |
| Voltage divider (10 kΩ / 15 kΩ) | Scales the 5 V sensor output to the ESP32's 3.3 V ADC range |
| BMP280 (I2C) | Pressure in the breath channel: breath and contamination detection |
| DHT22 | Ambient temperature/humidity for MQ-3 compensation |
| WS2812 LED ring (15 LEDs) | Status indicator (idle, breath fill animation, result color…) |
| DFPlayer Mini + speaker | Voice guidance and result announcement |
| Fan + MOSFET | Purges the breath channel after a measurement or on contamination |
| Push button | Starts a measurement / hold 10 s for R0 calibration |

### Pinout

| Signal | ESP32 pin |
|---|---|
| MQ-3 A0 (via voltage divider) | GPIO34 (ADC1) |
| TFT MOSI / MISO / SCLK | GPIO23 / GPIO19 / GPIO18 |
| TFT CS / DC / RST | GPIO15 / GPIO2 / GPIO4 |
| BMP280 SDA / SCL (addr 0x76, fallback 0x77) | GPIO21 / GPIO22 |
| DHT22 | GPIO32 |
| WS2812 data | GPIO33 |
| DFPlayer TX → ESP32 RX / DFPlayer RX → ESP32 TX (UART2, 9600 baud) | GPIO16 / GPIO17 |
| Fan (MOSFET gate) | GPIO26 |
| Button (internal pull-up) | GPIO27 |

> GPIO4 is taken by the display's RST line, so the breath sensor is an I2C BMP280 rather than a second single-wire DHT.

---

## How It Works

### 1. Sensor resistance

The ADC is read with `analogReadMilliVolts()` at `ADC_11db` attenuation. Each measurement averages 20 samples (oversampling) to reduce noise. The pin voltage is scaled back by the divider ratio (`(10k+15k)/15k ≈ 1.667`) and converted to sensor resistance:

```
Rs = (Vcc × RL / Vsensor) − RL        (Vcc = 5 V, RL = 200 kΩ)
```

### 2. Temperature/humidity compensation

The resistance of MOS sensors drifts with ambient temperature and humidity. The "Rs/R0 vs. temperature/humidity" chart from the MQ-3 datasheet was digitized into a table: 3 humidity curves (30 / 60 / 85 % RH) with 13 points each from −10 °C to 50 °C. The live DHT22 reading is looked up with **bilinear interpolation**, first along the temperature axis and then along the humidity axis. The resulting factor is divided by the factor at the calibration reference conditions, and `Rs/R0` is corrected by that ratio.

### 3. Concentration and per mille

The corrected ratio is converted to mg/L with a power-law fit of the datasheet's "Alcohol" curve. The ~2100:1 breath-to-blood ratio then gives per mille:

```
mg/L   = a × (Rs/R0)^(−1.4941)
‰     ≈ mg/L × 2.1
```

### 4. Measurement state machine

```
BEKLE (idle) ──button──▶ TABAN_OLC (baseline) ──▶ OLCUM (wait for breath → 6 s analysis)
      ▲   │                                              │
      │   │ hold 10 s                                    ▼
      │   ▼                                         SONUC (result + fan)
      │ R0_KALIBRASYON                                   │
      │ (purge → settle → sample → save to NVS)          ▼
      └──────────────────────────────────────── KANAL_TEMIZLE (fan purge 10 s)
```

- **Baseline** is re-measured before every measurement (~1.5 s).
- **Breath detection** uses the BMP280 pressure spike, or an MQ-3 threshold if the BMP280 is faulty. Detection opens a 6 s analysis window.
- **The result is a peak-hold value.** A reading only counts after **3 consecutive** samples above the threshold, so single noise spikes cannot reach the result.
- **0.50 ‰ and above** is shown as a red warning.

### 5. Contamination detection and sensor fault handling

- **Primary:** the BMP280's live pressure is compared with its own slow EMA baseline. A deviation above the threshold marks the channel as contaminated.
- **Fault detection:** the BMP280 is treated as faulty if it is not found on I2C, if its value does not change for 10 minutes (a frozen reading) or if it reports a physically impossible pressure.
- **Fallback:** when the BMP280 is faulty, contamination is detected from the raw MQ-3 reading instead. This fallback is disabled during a measurement so that a real breath is never mistaken for contamination.

### 6. Reliability

- **Task Watchdog:** `setup()` and `loop()` share one FreeRTOS task. Every blocking loop feeds the watchdog, and a real hang resets the device. See [`ui/Watchdog/WATCHDOG_ENTEGRASYONU.md`](ui/Watchdog/WATCHDOG_ENTEGRASYONU.md) (Turkish).
- **NVS:** R0, the calibration reference temperature/humidity and a measurement counter are stored persistently. The watchdog is fed around flash writes because they can be slow.

---

## Software

- Arduino IDE / ESP32 Arduino Core
- [LVGL](https://lvgl.io/) 9.3 — UI designed in SquareLine Studio
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — ST7796 driver, configured in `libraries/TFT_eSPI/User_Setup.h`
- Adafruit BMP280, DHT sensor library, Adafruit NeoPixel, DFRobotDFPlayerMini

### Repository layout

```
ui/
├── T-HMI/
│   ├── ui/
│   │   ├── ui.ino                    # Main firmware: watchdog, sensors, compensation, state machine
│   │   ├── led.cpp / led.h           # WS2812 status ring (table-driven, non-blocking animations)
│   │   ├── ses.cpp / ses.h           # DFPlayer Mini voice prompt queue
│   │   ├── SENSOR_DAVRANIS_NOTU.md   # Sensor behavior notes
│   │   ├── LED_DAVRANIS_NOTU.md      # State → LED mapping
│   │   ├── SES_DAVRANIS_NOTU.md      # State → audio mapping
│   │   └── SERIAL_LOG_HARITASI.md    # Map of all debug output
│   └── libraries/                    # Libraries used + lv_conf.h
├── ADC_kalibrasyon/                  # Standalone sketch for finding R0 in clean air
└── Watchdog/                         # Watchdog integration write-up
```

Code comments and design notes are in Turkish.

### Build

1. Install ESP32 board support in the Arduino IDE. Copy the libraries from `ui/T-HMI/libraries/` into your Arduino `libraries` folder.
2. Check that the driver and SPI pins in `TFT_eSPI/User_Setup.h` match the pinout above.
3. Make sure the UI files exported from SquareLine Studio (`ui.h`, `ui_ekran*.c`) are present under `libraries/ui`.
4. Copy the audio files to the DFPlayer SD card (`01/` general prompts, `02/` result announcements).
5. Build and flash `ui/T-HMI/ui/ui.ino`, then open the Serial Monitor at 115200 baud.
6. On first use, power on in clean air and hold the button for 10 s to run R0 calibration.

---

## Lessons Learned

- **The breath sensor changed twice (DHT11 → BME280 → BMP280).** Contamination detection started out comparing humidity between the breath channel and ambient air, with the DHT11 read once per second. Switching to pressure spikes brought detection down to 100 ms. Since humidity was no longer used, the simpler and cheaper BMP280 was enough.
- **Permanent "channel contaminated" lockup at boot.** The pressure baseline was hard-coded to sea-level standard (1013.25 hPa). Whenever local pressure was a few hPa away from that, the device went straight into an endless purge loop after power-on. The fix seeds the baseline from the first real reading.
- **Noise leaking into the result.** Peak accumulation used to start as soon as the raw MQ-3 crossed the threshold, so noise from before the analysis phase could end up in the result. Accumulation now waits until the analysis phase has actually started.
- **Pin conflicts.** The GPIO4/TFT RST conflict directly drove the choice of breath sensor.
- **Field hangs** were addressed with the watchdog and reset-reason logging.

## Limitations

- The MQ-3 is a metal-oxide (MOS) sensor and is not specific to ethanol. Perfume, sanitizer and other volatile compounds can also trigger it.
- The compensation table was digitized by eye from the datasheet chart. The contamination threshold (`KANAL_KIRLILIK_ESIK_HPA`) should be recalibrated on the final hardware.
- For education and development only. Do not use it for legal decisions such as fitness to drive.

## Roadmap

- [ ] Move from breadboard to a custom PCB
- [ ] Comparative calibration against a certified commercial breathalyzer
- [ ] Replace the compensation table with the datasheet's numeric data
- [ ] Measurement history logging / export
