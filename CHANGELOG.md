# Changelog

All notable changes to **Alfred — the reminder** are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.0] — 2026-07-14

First public release. A set-and-forget daily reminder appliance for the
Waveshare ESP32-C6-LCD-1.69, plus a companion wireless doorbell button.

### Added
- **Daily reminders** — full-screen icon + label at each scheduled time, with a
  chime then your chosen sound, ringing (on/off cycle) until you **tap** the
  device to dismiss (IMU tap detection — no touchscreen needed).
- **Self-setting clock** — syncs from the internet (NTP) at boot and hourly, and
  keeps time across power cuts via the onboard PCF85063 RTC.
- **Wireless doorbell** — a separate battery button (see `firmware/DoorbellButton`)
  deep-sleeps until pressed, then broadcasts a ring over **ESP-NOW**; the display
  shows a door screen + ding-dong + light flash. No pairing, no broker, no cloud.
- **Web config page** (served on the device's IP) — reminders, per-time-of-day
  **sleep schedule** (On / Display-off / Deep-sleep), **wake-up mode**, ring
  duration, low-battery power-saving thresholds, tap sensitivity with a live
  readout, and a ring log. Upload custom **sounds (WAV)** and **icons (PNG/JPG,
  converted in-browser)**. Everything is saved on-device — no reflashing.
- **Wake-up mode** — a silent "Awake?" prompt; tapping it starts your day and
  shifts the whole schedule to line up from your wake time (keeping your gaps).
- **Power & battery** — battery percentage on screen, soft power-latch handling,
  scheduled display-off / low-power sleep windows, and USB = always-full-power.
- **Browser web flasher** — flash the firmware straight from the GitHub Pages
  site over Web Serial; pick a published version or upload a `.bin`, no downloads.
- **One-click desktop flashers** — Windows/macOS/Linux zips with a bundled
  esptool, attached to each release.

### Security / Privacy
- **Wi-Fi is now set up from your phone via a captive portal**
  ([tzapu/WiFiManager](https://github.com/tzapu/WiFiManager)). **No Wi-Fi name or
  password is stored in the firmware**, so nothing private ships in a build or a
  release. On first boot the device opens an **`Alfred-Setup`** hotspot; a new
  **Reset Wi-Fi** button reopens it to switch networks. Credentials are saved
  on-device only.

[1.0.0]: https://github.com/HTerminal/Alfred-the-reminder/releases/tag/v1.0.0
