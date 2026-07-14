#pragma once
#include <lvgl.h>

// Runtime, web-editable schedule (loaded from /config.json, else built-in default).
#define MAX_REM 24

struct Rem {
  uint8_t  hour, minute;
  char     name[32];
  char     sound[44];    // "/sounds/x.wav" or "" (chime only)
  char     icon[32];     // icon key: built-in name, or uploaded "<file>.bin" (needs room for the full name)
  uint32_t accent;       // 0xRRGGBB
};

extern Rem   g_schedule[MAX_REM];
extern int   g_scheduleCount;
extern float g_tapThreshold;   // accel jerk (g) to count as a tap; lower = lighter tap

// Web-configurable daily POWER schedule: time windows that each pick a display/sleep mode.
#define MAX_PWIN 12
struct PowerWin { uint16_t start; uint16_t end; uint8_t mode; };  // minutes-of-day [start,end); mode = PowerMode
extern PowerWin g_power[MAX_PWIN];
extern int      g_powerCount;
extern int      g_ringSecs;      // doorbell/ring duration in seconds
extern int      g_battDispOff;   // force Display-Off at/below this battery % (0 = disabled)
extern int      g_battDeepSleep; // force Deep-Sleep (light sleep) at/below this % (0 = disabled)
extern int      g_wifiMode;      // 0 = WiFi always on; 1 = sync time at boot then disconnect (keep ESP-NOW)
extern int      g_wakeupMode;    // 1 = silent "Awake?" prompt; tap shifts the day's reminders to wake time

int power_mode_now(int minutesOfDay);   // PowerMode active at that minute-of-day (PWR_ON if uncovered)

void schedule_load();      // /config.json -> g_schedule (falls back to defaults)
bool schedule_save();      // g_schedule -> /config.json

const void *icon_big(const char *key);    // built-in dsc, or "L:/images/x.bin" path
const void *icon_home(const char *key);
uint32_t    icon_accent(const char *key);
const char *icon_keys();    // comma-separated list for the web UI
