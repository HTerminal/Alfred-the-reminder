#pragma once
#include <lvgl.h>

// Build the home screen + the (hidden) full-screen alert overlay.
void ui_init();

void ui_fs_init();   // register the LittleFS driver with LVGL (drive 'L')

// Home screen updaters. `icon` may be a built-in lv_img_dsc_t* OR a path string
// like "L:/images/x.bin" (lv_img_set_src accepts either).
void ui_update_clock(const char *hhmm, const char *ampm, const char *date);
void ui_update_next(const void *icon, const char *timestr, const char *name);
void ui_update_status(const char *text);   // small line (e.g. "syncing time…")
void ui_update_missed(int count);          // small "N missed" indicator (0 = hide)
void ui_update_battery(int pct);           // small battery gauge, top-right near the IP (<0 = hide)
void ui_update_sleep_badge(bool configured, bool onUsb);  // top-left "Zz" + USB status (when sleep is set)
void ui_boot_summary(const char *text);    // full-screen config summary at power-on
void ui_boot_summary_hide();
void ui_sleep_notice(const char *wakeText);  // "Going to sleep" + next wake, shown before deep sleep
void ui_hide_sleep_notice();
void ui_wake_prompt(bool show);            // silent full-screen "Awake? tap to start" (wake-up mode)

// Full-screen alert (reminder or doorbell). `pill` = the small top label.
void ui_show_alert(const void *icon, const char *pill,
                   const char *timestr, const char *name, uint32_t accent);
void ui_hide_alert();
bool ui_alert_active();
void ui_icon_wiggle(bool on);   // rock the alert icon side-to-side (doorbell)
