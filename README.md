# 🖨️ BambuTagger-Console

A touchscreen printer dashboard for **Bambu Lab** 3D printers running on the **Sunton ESP32-8048S043** development board (ESP32-S3, 800×480 RGB display).

---

## Features

| Feature | Details |
|---|---|
| 🔗 Local MQTT | Connects directly to the printer on your LAN (no cloud) |
| 📊 Live status | Nozzle / bed / chamber temps, progress bar, layer counter, remaining time |
| 🖼️ Thumbnail | Downloads the print thumbnail via FTPS and shows it during a print |
| 🎨 Dark UI | Clean dark theme with green Bambu-style accents |
| ⚙️ Config screen | On-screen keyboard to set WiFi + printer credentials — saved to NVS |
| 💾 Persistent settings | All credentials survive reboots (stored in ESP32 NVS) |

---

## Hardware

| Component | Details |
|---|---|
| Board | Sunton ESP32-8048S043 |
| MCU | ESP32-S3 (240 MHz, dual-core) |
| Display | 800 × 480 RGB (16-bit), ~60 fps |
| Touch | GT911 (capacitive, I²C) |
| PSRAM | 8 MB OPI (used for frame buffers + thumbnail) |
| Flash | 16 MB |

---

## Quick Start

### 1. Install PlatformIO
```bash
pip install platformio
```

### 2. Clone / open this project
```bash
cd BambuTagger-Console
pio run           # build
pio run -t upload # flash
```

### 3. First-time setup (on the touchscreen)

Tap the **⚙️ Config** icon in the left sidebar and fill in:

| Field | Where to find it |
|---|---|
| WiFi SSID & Password | Your home router |
| Printer IP | Printer touchscreen → Network → IP |
| Serial Number | Printer touchscreen → About |
| Access Code | Printer touchscreen → Network → Access code |

Tap **Save & Connect** — the board will reconnect and the status screen will populate within seconds.

---

## UI Layout

```
┌───────┬─────────────────────────────────────────────────┐
│  [B]  │  MyModel.3mf                       [RUNNING]    │
│       │  ████████████████░░░░░░░░░░  72%                │
│ [📋]  │ ─────────────────────────────────────────────── │
│       │ ┌────────┐   🌡 Nozzle   220°C / 220°C          │
│  [⚙]  │ │        │   🏠 Bed       55°C /  55°C          │
│       │ │ THUMB  │   🔄 Chamber   32°C                  │
│ [WiFi]│ │        │                                      │
│       │ └────────┘   📋 Layers   72 / 100               │
│       │              🔄 Remaining  0h 43m               │
│       │              ▶ Speed     100%                   │
└───────┴─────────────────────────────────────────────────┘
  80px                     720px
```

---

## Printer Connection Details

| Parameter | Value |
|---|---|
| Protocol | MQTT over TLS |
| Port | 8883 |
| Username | `bblp` (fixed) |
| Password | Printer Access Code |
| Subscribe | `device/<SERIAL>/report` |
| Publish | `device/<SERIAL>/request` |
| Thumbnail | FTPS (implicit TLS, port 990) at `/cache/` |

> **Note:** The board skips TLS certificate verification since Bambu uses a self-signed certificate. All communication stays on your local network.

---

## Project Structure

```
BambuTagger-Console/
├── platformio.ini          # Build config
├── include/
│   └── lv_conf.h           # LVGL 8.3 configuration
└── src/
    ├── config.h            # Default credentials (compile-time)
    ├── main.cpp            # Entry point, FreeRTOS tasks
    ├── display/
    │   └── display_driver.h   # LovyanGFX + LVGL bridge (800×480)
    ├── bambu/
    │   ├── bambu_client.h/.cpp   # MQTT status client
    │   └── ftps_client.h/.cpp    # Thumbnail downloader (FTPS)
    └── ui/
        ├── ui_manager.h/.cpp     # Sidebar + screen switcher
        ├── screen_status.h/.cpp  # Printer status screen
        └── screen_config.h/.cpp  # Configuration screen
```

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| Display stays black | Check backlight pin (GPIO 2) and PSRAM allocation |
| Touch not responding | GT911 I²C address may be `0x14` instead of `0x5D` — change in `display_driver.h` |
| MQTT won't connect | Verify printer IP, serial, and access code; ensure printer is on same LAN |
| Thumbnail missing | Some firmware versions serve different paths; check serial monitor logs |
| PSRAM alloc fails | Ensure `board_build.psram_type = opi` and `board_build.arduino.memory_type = qio_opi` in platformio.ini |

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| LovyanGFX | ^1.1.16 | Display driver + JPEG decode |
| LVGL | ^8.3.11 | UI framework |
| PubSubClient | ^2.8 | MQTT client |
| ArduinoJson | ^7.0.4 | JSON parsing |

