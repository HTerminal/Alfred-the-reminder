// =====================================================================
//  ReminderESP32  ·  Daily food/routine reminder appliance
//  Board : Waveshare ESP32-C6-LCD-1.69 (ST7789V2, 240x280)
//  Stack : Arduino_GFX + LVGL 8.3
//
//  What it does, with zero user intervention:
//    - sets its own clock from the internet (NTP over Wi-Fi), re-syncs hourly
//    - shows a big clock + the next upcoming reminder
//    - at each scheduled time it shows a full-screen icon + label and
//      rings (10s on / 10s off) until you TAP the device (IMU) to dismiss
//    - repeats every day, forever
//
//  See README.md for libraries, lv_conf.h settings and flashing steps.
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>          // tzapu/WiFiManager — captive-portal Wi-Fi setup (no hardcoded creds)
#include <time.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <LittleFS.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>

#include "config.h"
#include "ui.h"
#include "schedule.h"
#include "src/images/images.h"
#include "rtc.h"
#include "audio.h"
#include "imu.h"
#include "espnow.h"
#include "webconfig.h"
#include "ringlog.h"

// ------------------------- display driver ----------------------------
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED /*MISO*/);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, LCD_ROTATION, true /*IPS*/,
                                      LCD_W, LCD_H,
                                      LCD_COL_OFFSET, LCD_ROW_OFFSET,
                                      LCD_COL_OFFSET, LCD_ROW_OFFSET);

// ------------------------- LVGL plumbing -----------------------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         lvbuf[LCD_W * 40];   // ~40 line partial buffer

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(disp);
}

// ------------------------- scheduler state ---------------------------
static int      firedYday[MAX_REM];
static bool     timeReady   = false;
static bool     webStarted  = false;
static uint32_t lastTick    = 0;
static uint32_t lastSecond  = 0;

// ------------------------- alert / ring state ------------------------
static AlertKind alertKind      = KIND_REMINDER;   // enum in config.h
static uint32_t  alertStart     = 0;
static uint32_t  ringPhaseStart = 0;
static uint32_t  lastChime      = 0;
static bool      ringing        = true;   // true = ringing phase, false = quiet phase
static int       missedCount    = 0;      // reminders superseded before being tapped
static const char *currentSound = nullptr; // WAV to play for the active alert

// ------------------------- hardware state ----------------------------
static bool          haveRTC   = false;
static bool          haveAudio = false;
static bool          haveIMU   = false;
static int           i2cSDA = -1, i2cSCL = -1;
static volatile bool ntpSynced = false;      // set from the SNTP callback

// ------------------------- power / sleep state -----------------------
static bool     s_blOn            = true;    // backlight on/off
static uint32_t displayWakeUntil  = 0;       // Display-Off mode: keep screen lit until this millis()
static uint32_t bootGrace         = 0;       // no deep sleep before this millis() (web-config window)
static uint32_t bootSummaryUntil  = 0;       // show the config summary until this millis()
static bool     deepNoticeShown   = false;   // "going to sleep" notice shown for this deep-sleep entry
static bool     wakeTapped        = false;   // wake-up mode: user tapped "Awake?" for today
static int      wakeShift         = 0;       // minutes to shift each reminder (wake time - first reminder)
static int      wakeDay           = -1;      // yday of the last wake tap (daily re-arm)
static int      s_pmode           = PWR_ON;  // cached current power mode
static int      s_battPct         = 100;     // cached battery % (for low-battery power saving)
static bool     wokeFromTimer     = false;   // this boot came from a deep-sleep timer wake
static bool     wifiParked        = false;   // sync-then-off mode: left the AP (ESP-NOW still on)
static bool     ntpEverSynced     = false;   // NTP has synced at least once this boot

static void backlight(bool on) {
  s_blOn = on;
  digitalWrite(LCD_BL, on ? (BL_ACTIVE_HIGH ? HIGH : LOW) : (BL_ACTIVE_HIGH ? LOW : HIGH));
}

// ------------------------- Wi-Fi (WiFiManager) -----------------------
//  No SSID/password lives in this firmware. WiFiManager tries the network
//  that was saved on the device last time; if there is none, it opens a
//  captive-portal hotspot (WIFI_AP_NAME) so the user enters Wi-Fi from a
//  phone. The LCD shows the hotspot name while the portal is open. Either
//  way it returns after WIFI_PORTAL_TIMEOUT so the reminder clock still
//  runs from the RTC even if setup is skipped.
static void connectWiFi() {
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
  wm.setConnectTimeout(20);                 // seconds to try the saved network
  wm.setAPCallback([](WiFiManager *m) {     // fires when the setup portal opens
    char msg[200];
    snprintf(msg, sizeof(msg),
             "Wi-Fi setup\n\nOn your phone, join\nthis Wi-Fi network:\n\n%s\n\nthen pick your\nhome network",
             WIFI_AP_NAME);
    ui_boot_summary(msg);                   // full-screen instructions
    lv_refr_now(NULL);                      // push to the panel now (loop isn't running yet)
  });
  // If a network was set up before, just try it and fall through to offline on
  // failure (the loop keeps retrying). Only hijack boot with the setup portal
  // when there are NO saved credentials — i.e. genuine first-time setup — so a
  // momentarily-down router doesn't trap the device in a portal every boot.
  if (wm.getWiFiSSID(true).length() > 0) wm.setEnableConfigPortal(false);
  bool ok = wm.autoConnect(WIFI_AP_NAME);
  WiFi.mode(WIFI_STA);                      // ensure STA for ESP-NOW even if the portal timed out
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  Serial.printf("[wifi] autoConnect=%d ip=%s\n", ok, WiFi.localIP().toString().c_str());
}

// Battery %: GPIO0 reads 1/3 of the pack voltage; average a few samples.
static int battery_percent() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BAT_ADC_PIN);
  mv = (mv / 8) * 3;                                    // undo the 1/3 divider
  int pct = (int)((long)(mv - BAT_MV_EMPTY) * 100 / (BAT_MV_FULL - BAT_MV_EMPTY));
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}

// "Plugged in / charging" detection. This board has no VBUS-sense pin, but the
// USB-CDC link reports connected whenever the board is plugged into a USB host
// (confirmed: reads 1 even mid-charge). We use that as the primary signal, and
// OR in the battery-at-float-voltage heuristic so a dumb wall charger that has
// topped the pack off is also treated as plugged.
static bool onExternalPower() { return (bool)Serial || s_battPct >= CHARGING_PCT; }

// true if the user has configured ANY sleep / power-saving (a non-On window or a
// low-battery threshold) — used to show the top-left sleep badge.
static bool sleepConfigured() {
  if (g_battDispOff > 0 || g_battDeepSleep > 0) return true;
  for (int i = 0; i < g_powerCount; i++) if (g_power[i].mode != PWR_ON) return true;
  return false;
}

// Build a concise "what will happen when" summary for the boot splash.
static void buildBootSummary(char *out, size_t n) {
  const char *mn[] = { "On", "Display off", "Deep sleep" };
  int p = snprintf(out, n, "SLEEP / POWER PLAN\n\n");
  if (g_wakeupMode) p += snprintf(out + p, n - p, "WAKE-UP MODE ON\ntap screen to start day\n\n");
  if (g_powerCount == 0) {
    p += snprintf(out + p, n - p, "Screen always On\n");
  } else {
    for (int i = 0; i < g_powerCount && p < (int)n - 1; i++) {
      int m = g_power[i].mode <= 2 ? g_power[i].mode : 0;
      p += snprintf(out + p, n - p, "%02d:%02d-%02d:%02d  %s\n",
                    g_power[i].start / 60, g_power[i].start % 60,
                    g_power[i].end / 60, g_power[i].end % 60, mn[m]);
    }
    p += snprintf(out + p, n - p, "other hrs:  On\n");
  }
  p += snprintf(out + p, n - p, "\nRing %ds", g_ringSecs);
  if (g_battDispOff > 0) p += snprintf(out + p, n - p, "   low-batt<%d%%", g_battDispOff);
  snprintf(out + p, n - p, "\n%d reminders  (USB=no sleep)", g_scheduleCount);
}

// effective minute-of-day a reminder fires at — shifted to the wake time in wake-up mode
static int effReminderMin(int i) {
  int m = g_schedule[i].hour * 60 + g_schedule[i].minute;
  if (g_wakeupMode && wakeTapped) m = (m + wakeShift) % 1440;
  return m;
}

// minutes until the next scheduled reminder (1..1440; 1441 if none)
static int minutesToNextReminder(int nowMin) {
  int best = 1441;
  for (int i = 0; i < g_scheduleCount; i++) {
    int d = effReminderMin(i) - nowMin; if (d <= 0) d += 1440;
    if (d < best) best = d;
  }
  return best;
}

// Low-power sleep for a "Deep sleep" window.
// IMPORTANT (from the board schematic): BAT_EN (GPIO15) is a soft power-latch, and
// the ESP32-C6 canNOT hold a high-power-domain pin through TRUE deep sleep — so real
// deep sleep would drop GPIO15, cut battery power, and the board would switch OFF
// (never waking). We therefore use LIGHT sleep, which RETAINS GPIO state (the latch
// stays HIGH) while still gating the CPU + backlight. We nap in 60s chunks and let
// the main loop re-check the schedule / fire due reminders on each wake.
// minutes until the next real deep-sleep wake (next reminder OR when the window ends)
static int nextDeepWakeMin(int nowMin) {
  int rem = minutesToNextReminder(nowMin);
  int bnd = 1441;
  for (int d = 1; d <= 1440; d++) if (power_mode_now((nowMin + d) % 1440) != PWR_DEEP_SLEEP) { bnd = d; break; }
  return rem < bnd ? rem : bnd;
}

// format the "wakes at ..." line for the notice
static void buildWakeText(char *out, size_t n, int wmin) {
  time_t nowt = time(nullptr);
  if (nowt > 1000000000 && wmin > 0 && wmin < 1440) {
    time_t waket = nowt + (time_t)wmin * 60;
    struct tm wtm; localtime_r(&waket, &wtm);
    char hm[16]; strftime(hm, sizeof(hm), "%I:%M %p", &wtm);
    snprintf(out, n, "Wakes ~%s\n(in %d min)", hm, wmin);
  } else {
    snprintf(out, n, "Wakes in %d min", wmin);
  }
}

// show the "Going to sleep" notice for 5s, keeping LVGL rendering
static void showSleepNotice(const char *wakeText) {
  ui_sleep_notice(wakeText);
  uint32_t t0 = millis();
  while (millis() - t0 < 5000) { lv_timer_handler(); delay(20); }
}

// TRUE deep sleep, timer wake. Kept STANDARD so it reliably wakes (on USB the chip
// wakes on the timer and reboots). Keep BAT_EN HIGH so we don't cut our own power.
static void enterTrueDeepSleep(uint32_t seconds) {
  char wt[48];
  time_t nowt = time(nullptr);
  if (nowt > 1000000000) {
    time_t waket = nowt + 5 + seconds;                 // +5s notice
    struct tm wtm; localtime_r(&waket, &wtm);
    char hm[20]; strftime(hm, sizeof(hm), "%I:%M:%S %p", &wtm);
    snprintf(wt, sizeof(wt), "Wakes ~%s\n(in %us)", hm, (unsigned)seconds);
  } else snprintf(wt, sizeof(wt), "Wakes in %us", (unsigned)seconds);
  showSleepNotice(wt);                                  // 5s "Going to sleep" screen

  Serial.printf("[deepsleep] %us, timer wake\n", seconds);
  audio_stop();
  digitalWrite(BAT_EN_PIN, HIGH);                       // keep the latch held
  gfx->fillScreen(RGB565_BLACK);                        // blank the screen
  backlight(false);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();                               // wakes on the timer and resets
}

static void enterLowPowerSleep() {
  digitalWrite(BAT_EN_PIN, HIGH);                      // make sure the power latch stays held
  backlight(false);
  audio_stop();
  Serial.flush();
  esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);   // 60 s, then re-evaluate
  esp_light_sleep_start();                             // CPU halts; RETURNS on wake (no reboot)
  uint32_t t = millis();                               // resync the loop timers after the gap
  lastTick = lastSecond = t;
}

static void onNtpSync(struct timeval *) { ntpSynced = true; }

// Find the I2C bus by probing candidate SDA/SCL pairs for the RTC (0x51).
static bool i2cAutodetect() {
  const int cand[][2] = I2C_CANDIDATES;
  const int n = sizeof(cand) / sizeof(cand[0]);
  for (int i = 0; i < n; i++) {
    Wire.end();
    Wire.begin(cand[i][0], cand[i][1]);
    Wire.setClock(100000);
    delay(5);
    Wire.beginTransmission(RTC_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
      i2cSDA = cand[i][0]; i2cSCL = cand[i][1];
      return true;
    }
  }
  // nothing answered as RTC — fall back to the first pair so audio can still try
  const int fallback0 = cand[0][0], fallback1 = cand[0][1];
  Wire.end(); Wire.begin(fallback0, fallback1); Wire.setClock(100000);
  i2cSDA = fallback0; i2cSCL = fallback1;
  return false;
}

static void i2cScanPrint() {
  Serial.printf("[i2c] scanning bus SDA=%d SCL=%d ...\n", i2cSDA, i2cSCL);
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      const char *who = a == RTC_I2C_ADDR ? " (PCF85063 RTC)"
                      : a == ES8311_I2C_ADDR ? " (ES8311 codec)"
                      : a == 0x6B ? " (QMI8658 IMU)" : "";
      Serial.printf("[i2c]   found 0x%02X%s\n", a, who);
    }
  }
}

// format an hour/minute pair as "8:30 AM"
static void fmt12(int h, int m, char *out, size_t n) {
  int h12 = h % 12; if (h12 == 0) h12 = 12;
  snprintf(out, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
}

// If an untapped reminder is on screen when a new alert arrives, it's "missed".
static void noteSupersede() {
  if (ui_alert_active() && alertKind == KIND_REMINDER) {
    missedCount++;
    ui_update_missed(missedCount);
    Serial.printf("[alert] reminder missed (total %d)\n", missedCount);
  }
}

#define REMINDER_CHIME "/sounds/chime.wav"

// Ring (chime) first, THEN the spoken prompt. Falls back to a generated chime.
static void playReminderAudio() {
  if (!haveAudio) return;
  bool ok = currentSound ? audio_play_seq(REMINDER_CHIME, currentSound)  // chime -> voice
                         : audio_play_file(REMINDER_CHIME);              // chime only
  if (!ok) audio_chime();
}

static void startAlert(const Rem &r, AlertKind kind, int dispMin) {
  backlight(true);                                    // wake the screen (Display-Off / Deep-Sleep modes)
  displayWakeUntil = millis() + DISPLAY_WAKE_SECONDS * 1000UL;
  if (kind != KIND_DEMO) noteSupersede();
  char t[12];
  fmt12(dispMin / 60, dispMin % 60, t, sizeof(t));    // shown time (shifted in wake-up mode)
  ui_show_alert(icon_big(r.icon), "REMINDER", t, r.name, r.accent);
  lv_refr_now(NULL);
  alertKind = kind;
  alertStart = ringPhaseStart = millis();
  ringing = true;
  currentSound = r.sound[0] ? r.sound : nullptr;
  imu_arm();
  playReminderAudio();
  lastChime = millis();
}

static void startDoorbell() {
  backlight(true);                                    // wake the screen for the ring
  displayWakeUntil = millis() + DISPLAY_WAKE_SECONDS * 1000UL;
  noteSupersede();
  ui_show_alert(&img_doorbell, "DOORBELL", "", "Someone's at the door", 0xFF5A5A);
  lv_refr_now(NULL);
  ui_icon_wiggle(true);               // rock the bell instead of flashing the screen
  alertKind = KIND_DOORBELL;
  alertStart = lastChime = millis();
  currentSound = "/sounds/ring.wav";
  imu_arm();
  if (haveAudio && !audio_play_file(currentSound)) audio_dingdong();
  lastChime = millis();
  time_t nowt = time(nullptr);
  if (nowt > 1000000000) ringlog_add((uint32_t)nowt);   // log the ring if the clock is set
  Serial.println("[doorbell] ring!");
}

// Reminder = ring/quiet until tapped; doorbell = ding-dong + light flash, 10s.
static void serviceAlert() {
  if (!ui_alert_active()) return;
  uint32_t now = millis();

  bool dismiss = false;
  if (haveIMU && imu_tapped()) {
    dismiss = true;
    if (alertKind == KIND_REMINDER) { missedCount = 0; ui_update_missed(0); }
    Serial.println("[alert] tap -> dismissed");
  }
  if (now - alertStart >= (uint32_t)ALERT_MAX_SECONDS * 1000UL) dismiss = true;

  if (alertKind == KIND_DEMO) {
    if (dismiss || now - alertStart >= (uint32_t)DEMO_SECONDS * 1000UL) { audio_stop(); ui_hide_alert(); }
    return;
  }

  if (alertKind == KIND_DOORBELL) {
    if (dismiss || now - alertStart >= (uint32_t)g_ringSecs * 1000UL) {
      audio_stop(); ui_hide_alert(); return;          // wiggle stops inside ui_hide_alert
    }
    if (haveAudio && !audio_busy() && now - lastChime >= 300) {   // replay if it ends early
      if (!audio_play_file(currentSound)) audio_dingdong();
      lastChime = millis();
    }
    return;
  }

  // KIND_REMINDER
  if (dismiss) { audio_stop(); ui_hide_alert(); return; }
  if (ringing) {
    if (now - ringPhaseStart >= (uint32_t)RING_ON_SECONDS * 1000UL) {
      ringing = false; ringPhaseStart = now; audio_stop();        // -> quiet phase
    } else if (haveAudio && !audio_busy() && now - lastChime >= 1400) {
      playReminderAudio();                     // chime, then the voice prompt
      lastChime = millis();
    }
  } else {
    if (now - ringPhaseStart >= (uint32_t)RING_OFF_SECONDS * 1000UL) {
      ringing = true; ringPhaseStart = now;                        // -> ring again
    }
  }
}

// find the next reminder after the current minute-of-day and refresh the card
static void refreshNext(const struct tm &now) {
  if (g_scheduleCount == 0) { ui_update_next(nullptr, "--:--", "No reminders"); return; }
  if (g_wakeupMode && !wakeTapped) { ui_update_next(nullptr, "--:--", "Tap to start"); return; }
  int nowMin = now.tm_hour * 60 + now.tm_min;
  int best = 0, bestDelta = 100000, bestMin = 0;
  for (int i = 0; i < g_scheduleCount; i++) {
    int rmin = effReminderMin(i);              // shifted to wake time in wake-up mode
    int d = rmin - nowMin;
    if (d <= 0) d += 1440;           // wrap to tomorrow
    if (d < bestDelta) { bestDelta = d; best = i; bestMin = rmin; }
  }
  char t[12];
  fmt12(bestMin / 60, bestMin % 60, t, sizeof(t));
  ui_update_next(icon_home(g_schedule[best].icon), t, g_schedule[best].name);
}

// -------------------------------- setup ------------------------------
void setup() {
  // === CRITICAL, DO THIS FIRST ===
  // BAT_EN (GPIO15) is a soft power-LATCH (see schematic): on battery the board
  // stays powered ONLY while GPIO15 is HIGH. The power button momentarily turns
  // the battery MOSFET on; we must latch it HIGH before the button is released,
  // so drive it before anything else.
  gpio_hold_dis((gpio_num_t)BAT_EN_PIN);     // release any stale pad hold
  pinMode(BAT_EN_PIN, OUTPUT);
  digitalWrite(BAT_EN_PIN, HIGH);
  analogReadResolution(12);

  Serial.begin(115200);

  wokeFromTimer = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER);
  if (!wokeFromTimer) bootGrace = millis() + (uint32_t)BOOT_GRACE_SECONDS * 1000UL;

  // ---- boot banner: the first thing to check when debugging a unit ----
  //  Says exactly which build is on the board, how it woke, and how much
  //  room it has. Watch the serial monitor at 115200 baud.
  Serial.println();
  Serial.println("========================================");
  Serial.printf ("[boot] Alfred - the reminder  v%s\n", FW_VERSION);
  Serial.printf ("[boot] built    : %s %s\n", __DATE__, __TIME__);
  Serial.printf ("[boot] chip     : %s rev%d, %d MHz, %d core(s)\n",
                 ESP.getChipModel(), ESP.getChipRevision(),
                 (int)getCpuFrequencyMhz(), ESP.getChipCores());
  Serial.printf ("[boot] flash    : %u MB\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf ("[boot] free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf ("[boot] wake     : %s (reset reason %d)\n",
                 wokeFromTimer ? "deep-sleep timer" : "power-on / reset",
                 (int)esp_reset_reason());
  Serial.println("========================================");

  setenv("TZ", TZ_INFO, 1);   // so mktime() on RTC values is correct before NTP
  tzset();

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, BL_ACTIVE_HIGH ? HIGH : LOW);

  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, lvbuf, NULL, LCD_W * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = LCD_W;
  disp_drv.ver_res  = LCD_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  ui_init();
  ui_update_status("offline");                 // until WiFi connects (system runs from the RTC)
  s_battPct = battery_percent();
  ui_update_battery(s_battPct);                // show the gauge right away
  for (int i = 0; i < MAX_REM; i++) firedYday[i] = -1;

  // ---- I2C peripherals (RTC + audio codec share the bus) ----
  bool rtcFound = i2cAutodetect();
  i2cScanPrint();

#if ENABLE_RTC
  if (rtcFound) {
    haveRTC = rtc_begin(RTC_I2C_ADDR);
    if (haveRTC && rtc_time_valid()) {
      // Show the correct time immediately, before Wi-Fi/NTP is up.
      struct tm t;
      if (rtc_get(&t)) {
        time_t epoch = mktime(&t);            // TZ already set at top of setup()
        struct timeval tv = { epoch, 0 };
        settimeofday(&tv, nullptr);
        Serial.println("[rtc] clock loaded from onboard RTC");
      }
    }
  } else {
    Serial.println("[rtc] PCF85063 not detected on any candidate I2C pair");
  }
#endif

  haveAudio = audio_begin();
#if ENABLE_TAP_DISMISS
  haveIMU = imu_begin(i2cSDA, i2cSCL);
#endif

  // LittleFS holds the sound/image files and the saved schedule
  if (LittleFS.begin(true)) {
    Serial.printf("[fs] LittleFS: %u KB used / %u KB total\n",
                  (unsigned)(LittleFS.usedBytes() / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024));
  } else {
    Serial.println("[fs] LittleFS mount failed");
  }
  ui_fs_init();        // let LVGL load images from LittleFS ("L:/images/x.bin")
  if (!LittleFS.exists("/images")) LittleFS.mkdir("/images");
  schedule_load();     // /config.json (web-edited) or the built-in default
  ringlog_begin();     // last-100 doorbell ring history (shown on the web page)

  // On a real power-on (not a deep-sleep timer wake), show the config summary ~3s.
  if (!wokeFromTimer) {
    static char summary[440];
    buildBootSummary(summary, sizeof(summary));
    ui_boot_summary(summary);
    lv_refr_now(NULL);                 // draw it immediately
    bootSummaryUntil = millis() + 3000;
  }

  // Wi-Fi + internet time (self-setting clock). NTP is authoritative and,
  // whenever it syncs, we write the corrected time back into the RTC.
  sntp_set_time_sync_notification_cb(onNtpSync);
  WiFi.mode(WIFI_STA);
  connectWiFi();                        // WiFiManager: saved network, else phone setup portal
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

#if ENABLE_DOORBELL
  espnow_begin();                       // listen for the doorbell button
#endif

  lastTick = millis();

#if DEMO_ON_BOOT
  // Prove the alert works — but NOT on a deep-sleep timer wake (would fire every wake).
  if (!wokeFromTimer && g_scheduleCount > 0) startAlert(g_schedule[0], KIND_DEMO, effReminderMin(0));
#endif
}

// -------------------------------- loop -------------------------------
void loop() {
  // 1) drive LVGL
  uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();
  audio_service();                 // stream any WAV playback in progress
  if (haveIMU) imu_service();      // sample accel every loop (tap detection)

  if (bootSummaryUntil && millis() > bootSummaryUntil) {   // drop the boot summary after ~3s
    ui_boot_summary_hide();
    bootSummaryUntil = 0;
  }

  // 2) doorbell (ESP-NOW) + ring/quiet cycle + tap-to-dismiss
#if ENABLE_DOORBELL
  if (espnow_ring_pending()) startDoorbell();
#endif
  serviceAlert();

  // wake-up mode: tapping the "Awake?" prompt starts the day — the 1st reminder fires
  // now and every reminder shifts so they line up from the wake time (gaps preserved).
  if (g_wakeupMode && !wakeTapped && !ui_alert_active() && haveIMU && imu_tapped()) {
    struct tm tw;
    if (getLocalTime(&tw, 0) && (tw.tm_year + 1900) >= 2024 && g_scheduleCount > 0) {
      int fi = 0, anchor = 24 * 60;
      for (int i = 0; i < g_scheduleCount; i++) {
        int m = g_schedule[i].hour * 60 + g_schedule[i].minute;
        if (m < anchor) { anchor = m; fi = i; }       // the earliest reminder = the anchor
      }
      wakeShift = ((tw.tm_hour * 60 + tw.tm_min) - anchor + 1440) % 1440;
      wakeTapped = true; wakeDay = tw.tm_yday;
      for (int i = 0; i < MAX_REM; i++) firedYday[i] = -1;
      ui_wake_prompt(false);
      firedYday[fi] = tw.tm_yday;
      startAlert(g_schedule[fi], KIND_REMINDER, effReminderMin(fi));   // fire the 1st reminder now (at wake time)
      Serial.printf("[wake] tapped: shift %d min; first reminder now\n", wakeShift);
    }
  }

  // in Display-Off mode, a tap lights the screen briefly (chip is awake, IMU polled)
  if (s_pmode == PWR_DISPLAY_OFF && !ui_alert_active() && haveIMU && imu_tapped()) {
    displayWakeUntil = millis() + DISPLAY_WAKE_SECONDS * 1000UL;
    backlight(true);
  }

  // start the web server the first time WiFi connects (the system already runs
  // from the RTC clock without it; the IP/"offline" line is updated once a second)
  if (!webStarted && WiFi.status() == WL_CONNECTED) {
    webconfig_begin();
    webStarted = true;
  }
  if (webStarted) {
    webconfig_loop();
    if (webconfig_schedule_dirty()) {             // settings edited on the web
      for (int i = 0; i < MAX_REM; i++) firedYday[i] = -1;
      wakeTapped = false;                         // re-arm wake-up prompt instantly on save
      wifiParked = false;                         // re-evaluate WiFi after a change
      WiFi.setAutoReconnect(true);
      if (WiFi.status() != WL_CONNECTED) WiFi.begin();
      bootGrace = millis() + (uint32_t)BOOT_GRACE_SECONDS * 1000UL;   // keep web reachable a bit longer
    }
    if (webconfig_test_pending() && g_scheduleCount > 0)
      startAlert(g_schedule[0], KIND_DEMO, effReminderMin(0));   // web "Test alert" -> sample alert
    if (webconfig_sleep_pending())                 // web "Deep sleep now" -> lowest-power deep sleep, 20s
      enterTrueDeepSleep(20);                       // wakes in 20s on USB; powers OFF on battery
    if (webconfig_reboot_pending()) {              // web "Reboot"
      Serial.println("[web] reboot"); delay(200); ESP.restart();
    }
    if (webconfig_wifireset_pending()) {           // web "Reset Wi-Fi" -> forget the network, reopen setup portal
      Serial.println("[web] wifi reset");
      WiFiManager wm; wm.resetSettings();          // erase the saved credentials
      delay(200); ESP.restart();                    // reboot with no creds -> phone setup portal
    }
  }

  // 3) once per second: clock, Wi-Fi status, schedule check
  if (millis() - lastSecond >= 1000) {
    lastSecond = millis();

    // battery gauge, refreshed every ~5 s
    static int battTick = 4;
    if (++battTick >= 5) { battTick = 0; s_battPct = battery_percent(); ui_update_battery(s_battPct); }

    // While plugged in / charging, force full power: keep WiFi on + display on.
    bool plugged = onExternalPower();
    ui_update_sleep_badge(sleepConfigured(), plugged); // top-left "Zzz" + USB status
    static int pwrTick = 0;                             // debug log every ~10 s
    if (++pwrTick >= 10) { pwrTick = 0;
      Serial.printf("[pwr] plugged=%d usb-cdc=%d batt=%d%% wifiMode=%d parked=%d pmode=%d\n",
                    plugged, (int)(bool)Serial, s_battPct, g_wifiMode, wifiParked, s_pmode);
    }

    // status line + WiFi management.
    //  mode 0 (always on): stay connected, show IP; keep retrying if it drops.
    //  mode 1 (sync-then-off): connect at boot, get the time, then leave the AP to
    //   save power — but if plugged in, stay connected.
    // USB connected -> keep WiFi on: if it had parked (on battery earlier), bring it back.
    if (plugged && wifiParked) {
      wifiParked = false; WiFi.setAutoReconnect(true);
      if (WiFi.status() != WL_CONNECTED) WiFi.begin();
    }
    static uint32_t lastWifiTry = 0;
    if (g_wifiMode == 1 && wifiParked) {
      ui_update_status("wifi off");                    // (battery, sync-then-off): parked, ESP-NOW live
    } else if (WiFi.status() == WL_CONNECTED) {
      ui_update_status(WiFi.localIP().toString().c_str());
      // sync-then-off parks ONLY on battery; on USB we stay connected
      if (g_wifiMode == 1 && !plugged && millis() > bootGrace && (ntpEverSynced || millis() > 90000)) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false /* keep the radio on for ESP-NOW */);
        wifiParked = true;
        Serial.println("[wifi] sync-then-off: parked (ESP-NOW stays on)");
      }
    } else {
      ui_update_status("offline");
      if (millis() - lastWifiTry >= 20000) { lastWifiTry = millis(); WiFi.begin(); }
    }

    // NTP just corrected the clock -> persist it into the RTC
    if (ntpSynced) {
      ntpSynced = false;
      ntpEverSynced = true;                   // enables sync-then-off parking
      struct tm t;
      if (haveRTC && getLocalTime(&t, 0)) {
        rtc_set(&t);
        Serial.println("[rtc] updated from NTP");
      }
    }

    struct tm tnow;
    bool got = getLocalTime(&tnow, 0);
    if (got && (tnow.tm_year + 1900) >= 2024) {
      if (!timeReady) timeReady = true;   // status keeps showing the IP

      char hhmm[8], ampm[4], date[24];
      int h12 = tnow.tm_hour % 12; if (h12 == 0) h12 = 12;
      snprintf(hhmm, sizeof(hhmm), "%d:%02d", h12, tnow.tm_min);
      snprintf(ampm, sizeof(ampm), "%s", tnow.tm_hour < 12 ? "AM" : "PM");
      strftime(date, sizeof(date), "%a, %d %b", &tnow);
      ui_update_clock(hhmm, ampm, date);
      refreshNext(tnow);

      // wake-up mode: re-arm the "Awake?" prompt at the start of a new day
      if (g_wakeupMode && wakeTapped && tnow.tm_yday != wakeDay) wakeTapped = false;

      // fire reminders (once per day each). In wake-up mode nothing fires until the
      // user taps "Awake?"; after that they fire at shifted times (gaps preserved).
      int curMin = tnow.tm_hour * 60 + tnow.tm_min;
      for (int i = 0; i < g_scheduleCount; i++) {
        if (g_wakeupMode && !wakeTapped) break;           // waiting for the wake tap
        int fm = effReminderMin(i);                       // shifted to the wake time in wake-up mode
        if (curMin == fm && firedYday[i] != tnow.tm_yday) {
          firedYday[i] = tnow.tm_yday;
          startAlert(g_schedule[i], KIND_REMINDER, fm);   // ring until tapped
          break;
        }
      }

      // ---- power / sleep schedule ----
      // USB connected -> ALWAYS full power (screen on, never sleeps). On battery ONLY,
      // follow the web config: the scheduled window + the low-battery override.
      int nowMin = tnow.tm_hour * 60 + tnow.tm_min;
      int mode = PWR_ON;
      if (!plugged) {
        mode = power_mode_now(nowMin);
        if (g_battDispOff   > 0 && s_battPct <= g_battDispOff   && mode < PWR_DISPLAY_OFF) mode = PWR_DISPLAY_OFF;
        if (g_battDeepSleep > 0 && s_battPct <= g_battDeepSleep && mode < PWR_DEEP_SLEEP)  mode = PWR_DEEP_SLEEP;
      }
      s_pmode = mode;
      if (g_wakeupMode && !wakeTapped) s_pmode = PWR_ON;        // keep the screen on for the "Awake?" prompt
      if (s_pmode != PWR_DEEP_SLEEP) deepNoticeShown = false;   // re-arm the notice for next entry
      ui_wake_prompt(g_wakeupMode && !wakeTapped && !ui_alert_active());
      if (ui_alert_active() || s_pmode == PWR_ON) {
        backlight(true);
      } else if (s_pmode == PWR_DISPLAY_OFF) {
        backlight(millis() < displayWakeUntil);       // dark unless a recent tap/alert lit it
      } else {                                         // PWR_DEEP_SLEEP -> light sleep (see note)
        if (!audio_busy() && millis() > bootGrace && minutesToNextReminder(nowMin) > 1) {
          if (!deepNoticeShown) {                       // 5s "going to sleep" notice on first entry
            char wt[48]; buildWakeText(wt, sizeof(wt), nextDeepWakeMin(nowMin));
            showSleepNotice(wt); ui_hide_sleep_notice();
            deepNoticeShown = true;
          }
          enterLowPowerSleep();                        // naps ~60s, then returns
        } else {
          backlight(true);                             // boot-grace or imminent reminder -> stay lit
        }
      }
    }
    // (no time yet -> the clock just shows --:--; the status line above still
    //  reflects the network state. With a valid RTC this branch won't be hit.)
  }

  delay(2);
}
