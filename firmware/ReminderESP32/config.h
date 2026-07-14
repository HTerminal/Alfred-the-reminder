#pragma once
// =====================================================================
//  ReminderESP32  ·  user configuration
//  Board: Waveshare ESP32-C6-LCD-1.69  (ST7789V2, 240x280)
// =====================================================================

// Printed in the serial boot banner — handy when debugging a flashed unit
// ("which build is actually on this board?"). Watch it at 115200 baud.
// Bumping this is what cuts a new release: CI builds and publishes v<FW_VERSION>.
#define FW_VERSION      "1.0.1"

// ---------- Wi-Fi : set up once from your phone (no hardcoded password) ---
//  There is NO Wi-Fi name or password stored in this firmware, so nothing
//  private ever ships in a build or a release. On first boot (or after a
//  "Reset Wi-Fi") the device opens its own setup hotspot: join it from a
//  phone and pick your network — the credentials are then saved on the
//  device itself. Once connected it syncs time from the internet (NTP) at
//  boot and every hour, so the clock is set-and-forget for years and
//  survives power cuts.
//  The portal is NON-BLOCKING: the clock, reminders, doorbell and USB stay live
//  the whole time it is up, so the device never freezes waiting to be set up.
#define WIFI_AP_NAME        "Alfred-Setup"          // the setup hotspot you join from your phone
#define WIFI_PORTAL_TITLE   "Alfred - the reminder" // heading shown on the setup page
#define WIFI_RETRY_SECONDS  20                      // how often to retry a saved network when offline

// India Standard Time (UTC+5:30, no daylight saving).
// POSIX TZ format: name, then offset WEST of UTC -> IST is "IST-5:30".
#define TZ_INFO         "IST-5:30"
#define NTP_SERVER_1    "pool.ntp.org"
#define NTP_SERVER_2    "time.google.com"

// ---------- ST7789 LCD pins (Waveshare ESP32-C6-LCD-1.69) ------------
//  Verified against xiaozhi board config (78/xiaozhi-esp32,
//  boards/waveshare/esp32-c6-lcd-1.69/config.h).
#define LCD_SCLK        1
#define LCD_MOSI        2
#define LCD_DC          3
#define LCD_CS          5
#define LCD_RST         4
#define LCD_BL          6
#define BL_ACTIVE_HIGH  1        // backlight: 1 = HIGH turns it on
// (colour inversion is handled automatically by Arduino_ST7789 ips=true)

// ---------- Battery gauge (from Waveshare's ADC example) -------------
//  GPIO0 reads 1/3 of the pack voltage (so mV = analogReadMilliVolts*3).
//  GPIO15 is the battery-ENABLE latch: it must be held HIGH the whole
//  time (and kept high THROUGH deep sleep) or the board can lose power.
#define BAT_ADC_PIN     0
#define BAT_EN_PIN      15
#define BAT_MV_FULL     4200     // ~100 %
#define BAT_MV_EMPTY    3300     // ~0 %
// This board has no VBUS-sense pin, so "on charger / plugged in" is inferred from
// the battery sitting at the charger's float voltage. At/above this % we assume
// it's plugged in and keep WiFi + the display fully on. Tune via the [pwr] log.
#define CHARGING_PCT    95

#define LCD_W           240
#define LCD_H           280
#define LCD_ROTATION    2        // 2 = 180° (the panel reads upside-down at 0)
#define LCD_COL_OFFSET  0        // ST7789V2 240x280 GRAM window offsets
#define LCD_ROW_OFFSET  20       // if you see a thin band / shifted pixels, tweak this

// Alert kinds (defined here so the .ino's auto-generated prototypes see it).
enum AlertKind { KIND_REMINDER, KIND_DEMO, KIND_DOORBELL };

// Power/display modes for the web-configurable daily "sleep schedule".
//   PWR_ON          - screen on, everything running (default)
//   PWR_DISPLAY_OFF - backlight off but chip awake: still catches rings/alerts/taps
//   PWR_DEEP_SLEEP  - true deep sleep: wakes ONLY on the timer (a due reminder or
//                     the window's end); ESP-NOW rings and idle taps are missed
enum PowerMode { PWR_ON, PWR_DISPLAY_OFF, PWR_DEEP_SLEEP };
#define DISPLAY_WAKE_SECONDS 8    // Display-Off mode: a tap lights the screen this long
#define BOOT_GRACE_SECONDS   60   // on power-up, stay awake this long so the web is reachable

// ---------- Alert / ring behaviour -----------------------------------
//  A reminder RINGS for RING_ON_SECONDS, goes quiet for RING_OFF_SECONDS,
//  then rings again — repeating until you TAP the device to dismiss it
//  (tap detected by the onboard IMU, since there's no touchscreen).
#define RING_ON_SECONDS   10
#define RING_OFF_SECONDS  10
#define ALERT_MAX_SECONDS 300    // safety: auto-dismiss after 5 min if never tapped
#define FLASH_SECONDS     12     // accent-frame pulse duration
#define DEMO_ON_BOOT      0      // 1 = short sample alert at boot (use the web "Test alert" button instead)
#define DEMO_SECONDS      6

// ---------- Tap-to-dismiss (QMI8658 IMU) -----------------------------
#define ENABLE_TAP_DISMISS 1
#define TAP_JERK_G        0.9f   // accel jerk (g) that counts as a tap
#define TAP_DEBOUNCE_MS   600    // ignore repeat taps within this window
#define TAP_SETTLE_MS     500    // ignore taps for this long after an alert starts
// Hard floor for the web-set tap threshold. The QMI8658's own idle noise reads
// ~0.05-0.18 g of jerk, so anything at/below that turns sensor noise into a
// constant stream of phantom "taps" — which silently dismisses alerts and spams
// the log. A real finger tap measures ~0.9 g, so 0.20 g is still very sensitive.
#define TAP_MIN_G         0.20f

// ---------- ESP-NOW doorbell -----------------------------------------
//  A separate battery ESP (see firmware/DoorbellButton) sleeps until its
//  button is pressed, then broadcasts a "ring" over ESP-NOW. This display
//  receives it and shows the door screen + ding-dong + light flash for
//  DOORBELL_SECONDS. No WiFi pairing, no broker — pure ESP-NOW broadcast.
#define ENABLE_DOORBELL   1
#define DOORBELL_SECONDS  10
#define DOORBELL_MAGIC    0xB311BE11UL   // messages must carry this to ring

// ---------- I2C bus (shared by RTC + audio codec + IMU) --------------
//  Both the PCF85063 RTC (addr 0x51) and the ES8311 codec (addr 0x18)
//  sit on the same I2C bus. I don't have this board's exact SDA/SCL from
//  Waveshare, so the firmware AUTO-DETECTS the bus at boot: it tries the
//  candidate pairs below and keeps the one where it actually finds the
//  RTC. Watch the Serial monitor (115200) — it prints what it found.
//  If auto-detect fails, read SDA/SCL off your board's pinout and put the
//  correct pair FIRST in this list.
#define I2C_CANDIDATES  { {8,7}, {8,9}, {18,19}, {6,7}, {11,10}, {10,11} }  // {8,7}=this board
#define RTC_I2C_ADDR    0x51
#define ES8311_I2C_ADDR 0x18

// ---------- Onboard RTC (PCF85063) -----------------------------------
//  Keeps time across power cuts and when Wi-Fi is unavailable. On boot the
//  clock loads from the RTC immediately; whenever NTP syncs, the RTC is
//  updated from it. Set to 0 to ignore the RTC and use NTP only.
#define ENABLE_RTC      1

// ---------- Chime through the onboard ES8311 speaker -----------------
//  Plays a short chime when a reminder fires. The codec's *control* pins
//  (I2C) are auto-detected above, but its *data* (I2S) pins are board-
//  specific and I could NOT verify them for this board.
//  >>> VERIFY these 5 numbers against your board's pinout / Waveshare's
//      "15_ES8311" example before expecting sound. <<<
//  Cross-ref: github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 (15_ES8311)
//  I2S pins verified from the xiaozhi board config for this exact board.
#define ENABLE_CHIME    1
#define I2S_MCLK_PIN    19
#define I2S_BCLK_PIN    20
#define I2S_WS_PIN      22       // LRCK
#define I2S_DOUT_PIN    23       // to codec speaker output
#define I2S_DIN_PIN     21       // mic in (unused for playback)
#define PA_ENABLE_PIN   -1       // this board has no separate PA-enable pin
#define CHIME_VOLUME    90       // 0..100  (louder)
#define AUDIO_RATE      16000     // matches the 16 kHz WAV files (correct speed)
