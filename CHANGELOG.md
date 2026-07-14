# Changelog

All notable changes to **Alfred — the reminder** are documented here.
This project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.1] — 2026-07-14

Bug-fix release. Fixes found from a real serial log off the device.

### Fixed
- **Device reported "not plugged in" while plugged in.** Power detection used
  `(bool)Serial`, which is only true while a host *has the serial port open*
  (DTR) — so closing the serial monitor, or using a plain wall charger, read as
  "on battery" and the device would sleep or park Wi-Fi. Now uses the USB
  Serial/JTAG `isPlugged()` signal, which tracks real USB bus power.
- **Phantom taps from sensor noise.** The tap threshold could be set (0.05 g)
  *below the IMU's own noise floor* (idle jerk reads 0.05–0.18 g; a real finger
  tap is ~0.9 g), so noise registered as a tap every 600 ms — silently dismissing
  alerts, spuriously starting wake-up mode, and flooding the serial log. A 0.20 g
  floor is now enforced on config load, on web save, and in the IMU itself.
- **Firmware could not be uploaded, boot hung, and Wi-Fi stayed offline.** All
  one root cause: the Wi-Fi setup portal was *blocking*, freezing `setup()` for
  minutes with the SoftAP up — so the main loop never ran, USB went unresponsive
  (uploads failed), and if setup was never completed the device stayed offline
  forever. **The portal is now non-blocking**: the clock, reminders, doorbell and
  USB all stay alive while it waits to be set up.
- **Buffer overrun** in the boot-summary builder: `p += snprintf(...)` could push
  the offset past the buffer (snprintf returns what it *would* have written),
  underflowing the remaining length into a huge value.
- **Flat battery could report 100%** — unsigned underflow in the percentage maths.

### Changed
- The device now says **“Hotspot is LIVE — Alfred-Setup”** on screen while the
  setup portal is up, with the two steps to follow; the status line reads
  `setup: Alfred-Setup` instead of a misleading `offline`.
- The Wi-Fi setup page is branded **“Alfred - the reminder”** (was “WiFiManager”).
- Serial boot banner reduced to a single line.
- `build_upload.bat` retries once automatically and gives real recovery steps.

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

[1.0.1]: https://github.com/HTerminal/Alfred-the-reminder/releases/tag/v1.0.1
[1.0.0]: https://github.com/HTerminal/Alfred-the-reminder/releases/tag/v1.0.0
