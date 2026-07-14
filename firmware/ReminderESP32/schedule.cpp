#include "schedule.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "src/images/images.h"

Rem   g_schedule[MAX_REM];
int   g_scheduleCount = 0;
float g_tapThreshold  = 0.45f;   // lighter default (was a hard 0.9)

PowerWin g_power[MAX_PWIN];       // daily power/sleep windows (empty = always on)
int      g_powerCount = 0;
int      g_ringSecs   = 10;       // doorbell/ring duration (seconds)
int      g_battDispOff   = 10;    // low-battery: force Display-Off at/below this % (0 = off)
int      g_battDeepSleep = 0;     // low-battery: force Deep-Sleep at/below this % (0 = off)
int      g_wifiMode      = 0;     // 0 = always on; 1 = sync-then-off (keep ESP-NOW)
int      g_wakeupMode    = 0;     // 1 = wake-up mode (silent "Awake?" prompt, shifts reminders on tap)

static bool pw_contains(const PowerWin &w, int m) {
  if (w.start == w.end) return false;
  if (w.start < w.end)  return m >= w.start && m < w.end;
  return m >= w.start || m < w.end;      // window wraps past midnight
}
int power_mode_now(int m) {
  for (int i = 0; i < g_powerCount; i++) if (pw_contains(g_power[i], m)) return g_power[i].mode;
  return PWR_ON;                          // uncovered hours stay on
}

struct IconEntry { const char *key; const lv_img_dsc_t *big; const lv_img_dsc_t *home; uint32_t accent; };
static const IconEntry ICONS[] = {
  {"almonds",      &img_almonds_eat,  &img_almonds_eat_h,  0xD9A066},
  {"almonds_soak", &img_almonds_soak, &img_almonds_soak_h, 0x5AC8FA},
  {"milk",         &img_milk,         &img_milk_h,         0x8AB4F8},
  {"breakfast",    &img_breakfast,    &img_breakfast_h,    0xF2C14E},
  {"salad",        &img_salad,        &img_salad_h,        0x6FCF6F},
  {"lunch",        &img_lunch,        &img_lunch_h,        0xF2994A},
  {"chaat",        &img_chaat,        &img_chaat_h,        0xE0A458},
  {"coconut",      &img_coconut,      &img_coconut_h,      0x2FD6B0},
  {"dinner",       &img_dinner,       &img_dinner_h,       0xC77DFF},
  {"doorbell",     &img_doorbell,     &img_doorbell_h,     0xFF5A5A},
};
static const int NICONS = sizeof(ICONS) / sizeof(ICONS[0]);

uint32_t icon_accent(const char *key) {
  for (int i = 0; i < NICONS; i++) if (!strcmp(ICONS[i].key, key)) return ICONS[i].accent;
  return 0x8AB4F8;
}

const void *icon_big(const char *key) {
  if (!key || !key[0]) return nullptr;       // "" -> no image, big text-only alert
  if (strchr(key, '.')) {                    // uploaded image -> canonical "L:/images/<base>.bin"
    static char p[80], base[64];             // rebuild from the base so a truncated ".bi" still resolves
    strncpy(base, key, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    char *dot = strrchr(base, '.'); if (dot) *dot = 0;
    snprintf(p, sizeof(p), "L:/images/%s.bin", base); return p;
  }
  for (int i = 0; i < NICONS; i++) if (!strcmp(ICONS[i].key, key)) return ICONS[i].big;
  return ICONS[0].big;
}
const void *icon_home(const char *key) {
  if (!key || !key[0]) return nullptr;       // "" -> no icon on the home "up next" row
  if (strchr(key, '.')) {                    // "myimg.bin" -> "L:/images/myimg_h.bin"
    static char p[80], base[64];
    strncpy(base, key, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    char *dot = strrchr(base, '.'); if (dot) *dot = 0;
    snprintf(p, sizeof(p), "L:/images/%s_h.bin", base); return p;
  }
  for (int i = 0; i < NICONS; i++) if (!strcmp(ICONS[i].key, key)) return ICONS[i].home;
  return ICONS[0].home;
}
const char *icon_keys() {
  static char buf[192]; buf[0] = 0;
  for (int i = 0; i < NICONS; i++) { strcat(buf, ICONS[i].key); if (i < NICONS - 1) strcat(buf, ","); }
  return buf;
}

static void setRem(int i, uint8_t h, uint8_t m, const char *name, const char *sound, const char *icon, uint32_t accent) {
  g_schedule[i].hour = h; g_schedule[i].minute = m; g_schedule[i].accent = accent;
  strncpy(g_schedule[i].name,  name,  sizeof(g_schedule[i].name)  - 1); g_schedule[i].name[sizeof(g_schedule[i].name) - 1]   = 0;
  strncpy(g_schedule[i].sound, sound, sizeof(g_schedule[i].sound) - 1); g_schedule[i].sound[sizeof(g_schedule[i].sound) - 1] = 0;
  strncpy(g_schedule[i].icon,  icon,  sizeof(g_schedule[i].icon)  - 1); g_schedule[i].icon[sizeof(g_schedule[i].icon) - 1]   = 0;
}

static void loadDefaults() {
  g_powerCount = 0;          // always-on by default
  g_ringSecs   = 10;
  g_battDispOff = 10; g_battDeepSleep = 0; g_wifiMode = 0; g_wakeupMode = 0;
  g_scheduleCount = 10;
  setRem(0,  8,  0, "Eat soaked almonds",   "/sounds/almonds.wav",   "almonds",      0xD9A066);
  setRem(1,  8, 30, "Milk with protein",    "",                      "milk",         0x8AB4F8);
  setRem(2, 10, 30, "Breakfast",            "/sounds/breakfast.wav", "breakfast",    0xF2C14E);
  setRem(3, 12,  0, "Fruits / Salad",       "",                      "salad",        0x6FCF6F);
  setRem(4, 13, 30, "Lunch",                "/sounds/lunch.wav",     "lunch",        0xF2994A);
  setRem(5, 15,  0, "Channa / Fiber chaat", "",                      "chaat",        0xE0A458);
  setRem(6, 18,  0, "Coconut water",        "",                      "coconut",      0x2FD6B0);
  setRem(7, 19, 40, "Dinner",               "/sounds/dinner.wav",    "dinner",       0xC77DFF);
  setRem(8, 21,  0, "Soak almonds in water","/sounds/soak.wav",      "almonds_soak", 0x5AC8FA);
  setRem(9, 21, 40, "Milk with protein",    "",                      "milk",         0x8AB4F8);
}

void schedule_load() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) { loadDefaults(); Serial.println("[cfg] no config.json -> defaults"); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err || !doc["reminders"].is<JsonArray>()) { loadDefaults(); return; }

  g_scheduleCount = 0;
  for (JsonObject o : doc["reminders"].as<JsonArray>()) {
    if (g_scheduleCount >= MAX_REM) break;
    Rem &r = g_schedule[g_scheduleCount++];
    r.hour = o["h"] | 0; r.minute = o["m"] | 0;
    strncpy(r.name,  o["name"]  | "",     sizeof(r.name)  - 1); r.name[sizeof(r.name) - 1]   = 0;
    strncpy(r.sound, o["sound"] | "",     sizeof(r.sound) - 1); r.sound[sizeof(r.sound) - 1] = 0;
    strncpy(r.icon,  o["icon"]  | "milk", sizeof(r.icon)  - 1); r.icon[sizeof(r.icon) - 1]   = 0;
    const char *ac = o["accent"] | "8AB4F8";
    r.accent = strtoul(ac, nullptr, 16);
  }
  g_tapThreshold = doc["tapG"] | 0.45f;

  g_powerCount = 0;                                  // power/sleep windows
  for (JsonObject o : doc["power"].as<JsonArray>()) {
    if (g_powerCount >= MAX_PWIN) break;
    PowerWin &w = g_power[g_powerCount++];
    w.start = o["s"] | 0; w.end = o["e"] | 0; w.mode = o["m"] | 0;
  }
  g_ringSecs = doc["ringSecs"] | 10;
  g_battDispOff   = doc["battOff"]  | 10;
  g_battDeepSleep = doc["battDeep"] | 0;
  g_wifiMode      = doc["wifiMode"] | 0;
  g_wakeupMode    = doc["wakeMode"] | 0;

  if (g_scheduleCount == 0) loadDefaults();
  Serial.printf("[cfg] loaded %d reminders, %d power windows, ring=%ds, tapG=%.2f\n",
                g_scheduleCount, g_powerCount, g_ringSecs, g_tapThreshold);
}

bool schedule_save() {
  JsonDocument doc;
  JsonArray arr = doc["reminders"].to<JsonArray>();
  for (int i = 0; i < g_scheduleCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["h"] = g_schedule[i].hour; o["m"] = g_schedule[i].minute;
    o["name"] = g_schedule[i].name; o["sound"] = g_schedule[i].sound; o["icon"] = g_schedule[i].icon;
    char hex[8]; snprintf(hex, sizeof(hex), "%06X", (unsigned)(g_schedule[i].accent & 0xFFFFFF));
    o["accent"] = hex;
  }
  doc["tapG"] = g_tapThreshold;

  JsonArray pw = doc["power"].to<JsonArray>();
  for (int i = 0; i < g_powerCount; i++) {
    JsonObject o = pw.add<JsonObject>();
    o["s"] = g_power[i].start; o["e"] = g_power[i].end; o["m"] = g_power[i].mode;
  }
  doc["ringSecs"] = g_ringSecs;
  doc["battOff"]  = g_battDispOff;
  doc["battDeep"] = g_battDeepSleep;
  doc["wifiMode"] = g_wifiMode;
  doc["wakeMode"] = g_wakeupMode;

  File f = LittleFS.open("/config.json", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  Serial.println("[cfg] saved config.json");
  return true;
}
