// =====================================================================
//  ui.cpp  ·  world-class dark UI for the daily-reminder display
//  Pure-black background, minimal typography, food icon per reminder.
//  Home : big clock + date + a clean "UP NEXT" row
//  Alert: full-screen icon on black with a pulsing accent frame
// =====================================================================
#include "ui.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>

// ---- LVGL filesystem driver: lets lv_img load "L:/images/x.bin" from LittleFS ----
static void *fs_open(lv_fs_drv_t *, const char *path, lv_fs_mode_t) {
  File *f = new File(LittleFS.open(path, "r"));
  if (!f || !*f) { delete f; return nullptr; }
  return f;
}
static lv_fs_res_t fs_close(lv_fs_drv_t *, void *fp) { File *f = (File *)fp; f->close(); delete f; return LV_FS_RES_OK; }
static lv_fs_res_t fs_read(lv_fs_drv_t *, void *fp, void *buf, uint32_t btr, uint32_t *br) {
  *br = ((File *)fp)->read((uint8_t *)buf, btr); return LV_FS_RES_OK;
}
static lv_fs_res_t fs_seek(lv_fs_drv_t *, void *fp, uint32_t pos, lv_fs_whence_t w) {
  File *f = (File *)fp;
  uint32_t base = (w == LV_FS_SEEK_CUR) ? f->position() : (w == LV_FS_SEEK_END) ? f->size() : 0;
  f->seek(base + pos); return LV_FS_RES_OK;
}
static lv_fs_res_t fs_tell(lv_fs_drv_t *, void *fp, uint32_t *pos) { *pos = ((File *)fp)->position(); return LV_FS_RES_OK; }

static lv_fs_drv_t fsdrv;
void ui_fs_init() {
  lv_fs_drv_init(&fsdrv);
  fsdrv.letter = 'L';
  fsdrv.open_cb = fs_open; fsdrv.close_cb = fs_close;
  fsdrv.read_cb = fs_read; fsdrv.seek_cb = fs_seek; fsdrv.tell_cb = fs_tell;
  lv_fs_drv_register(&fsdrv);
}

// ---- home widgets ----
static lv_obj_t *lbl_clock, *lbl_ampm, *lbl_date, *lbl_status, *lbl_missed, *lbl_batt, *lbl_sleep, *divider;
static lv_obj_t *next_icon, *lbl_next_hdr, *lbl_next_time, *lbl_next_name;

// ---- alert widgets ----
static lv_obj_t *alert_layer, *alert_pill, *alert_icon, *alert_time, *alert_name;
static lv_timer_t *flash_timer;
static uint32_t   flash_accent;
static int        flash_ticks;

// The alert hero image is loaded fully into RAM (like the built-in icons) so it
// never has to be re-read from LittleFS while the audio task is streaming a WAV
// off the same flash. The buffer is allocated ONCE at boot (clean heap) and
// reused every alert — a per-alert malloc of ~76KB fails once WiFi/web/audio
// have fragmented the heap, which silently fell back to the (blank) file path.
static uint8_t     *s_alertBuf = nullptr;
static size_t       s_alertCap = 0;
static lv_img_dsc_t s_alertDsc;

#define COL_BG     0x000000      // pure black
#define COL_TEXT   0xFFFFFF
#define COL_DIM    0x6E7686      // muted grey
#define COL_LINE   0x18181C      // hairline
#define COL_ACCENT 0x5AC8FA      // soft blue accent for the clock's AM/PM

static void flash_cb(lv_timer_t *t) {
  flash_ticks++;
  if (flash_ticks > (FLASH_SECONDS * 1000 / 400)) {     // settle after FLASH_SECONDS
    lv_obj_set_style_border_width(alert_layer, 2, 0);
    lv_obj_set_style_border_opa(alert_layer, LV_OPA_COVER, 0);
    lv_timer_pause(t);
    return;
  }
  bool on = (flash_ticks & 1) == 0;
  lv_obj_set_style_border_width(alert_layer, on ? 3 : 1, 0);   // thin pulse
  lv_obj_set_style_border_opa(alert_layer, on ? LV_OPA_COVER : LV_OPA_40, 0);
}

// ---------------------------------------------------------------------
void ui_init() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ---- clock ----
  lbl_clock = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(lbl_clock, "--:--");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, -12, 40);

  lbl_ampm = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_ampm, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_ampm, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(lbl_ampm, "");
  lv_obj_align_to(lbl_ampm, lbl_clock, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -9);

  // ---- date ----
  lbl_date = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_date, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(lbl_date, "");
  lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 100);

  // ---- hairline divider ----
  divider = lv_obj_create(scr);
  lv_obj_set_size(divider, 196, 2);
  lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 140);
  lv_obj_set_style_bg_color(divider, lv_color_hex(COL_LINE), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(divider, 0, 0);
  lv_obj_set_style_radius(divider, 0, 0);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

  // ---- "UP NEXT" header ----
  lbl_next_hdr = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_next_hdr, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_next_hdr, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_letter_space(lbl_next_hdr, 3, 0);
  lv_label_set_text(lbl_next_hdr, "UP NEXT");
  lv_obj_align(lbl_next_hdr, LV_ALIGN_TOP_LEFT, 16, 150);

  // ---- next-reminder row: big native 104px icon + time + name ----
  next_icon = lv_img_create(scr);
  lv_obj_align(next_icon, LV_ALIGN_TOP_LEFT, 12, 166);   // native 104px, no zoom/clip

  lbl_next_time = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_next_time, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_next_time, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(lbl_next_time, "--:--");
  lv_obj_align(lbl_next_time, LV_ALIGN_TOP_LEFT, 128, 190);

  lbl_next_name = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_next_name, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_next_name, lv_color_hex(COL_DIM), 0);
  lv_label_set_long_mode(lbl_next_name, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_next_name, 104);
  lv_label_set_text(lbl_next_name, "");
  lv_obj_align(lbl_next_name, LV_ALIGN_TOP_LEFT, 128, 220);

  // ---- status (tiny, top-right) ----
  lbl_status = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(lbl_status, "");
  lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 6);   // shows the web IP once connected

  // ---- sleep badge (top-left corner): "Zzz" + USB/battery status, shown when sleep is configured ----
  lbl_sleep = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_sleep, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_sleep, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(lbl_sleep, "");
  lv_obj_align(lbl_sleep, LV_ALIGN_TOP_LEFT, 6, 4);

  // ---- "N missed" indicator (tiny, top-right under the battery) ----
  lbl_missed = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_missed, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_missed, lv_color_hex(0xFF6B6B), 0);
  lv_label_set_text(lbl_missed, "");
  lv_obj_align(lbl_missed, LV_ALIGN_TOP_RIGHT, -8, 26);

  // ---- battery gauge (tiny, top-right, next to the IP) ----
  lbl_batt = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_batt, lv_color_hex(COL_DIM), 0);
  lv_label_set_text(lbl_batt, "");
  lv_obj_align(lbl_batt, LV_ALIGN_TOP_RIGHT, -8, 8);

  // =================== full-screen alert overlay ===================
  alert_layer = lv_obj_create(lv_layer_top());
  lv_obj_set_size(alert_layer, LCD_W, LCD_H);
  lv_obj_align(alert_layer, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(alert_layer, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(alert_layer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(alert_layer, 2, 0);   // thin
  lv_obj_set_style_border_color(alert_layer, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_radius(alert_layer, 34, 0);   // rounded to match the curved panel
  lv_obj_set_style_pad_all(alert_layer, 0, 0);
  lv_obj_clear_flag(alert_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(alert_layer, LV_OBJ_FLAG_HIDDEN);

  alert_pill = lv_label_create(alert_layer);
  lv_obj_set_style_text_font(alert_pill, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(alert_pill, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_letter_space(alert_pill, 4, 0);
  lv_label_set_text(alert_pill, "REMINDER");
  lv_obj_align(alert_pill, LV_ALIGN_TOP_MID, 0, 4);

  alert_icon = lv_img_create(alert_layer);
  lv_obj_align(alert_icon, LV_ALIGN_TOP_MID, 0, 22);   // big native 196px

  alert_time = lv_label_create(alert_layer);
  lv_obj_set_style_text_font(alert_time, &lv_font_montserrat_20, 0);   // smaller
  lv_obj_set_style_text_color(alert_time, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(alert_time, "");
  lv_obj_align(alert_time, LV_ALIGN_TOP_MID, 0, 226);

  alert_name = lv_label_create(alert_layer);
  lv_obj_set_style_text_font(alert_name, &lv_font_montserrat_16, 0);   // smaller
  lv_obj_set_style_text_color(alert_name, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(alert_name, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(alert_name, 228);
  lv_obj_set_style_text_align(alert_name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(alert_name, "");
  lv_obj_align(alert_name, LV_ALIGN_TOP_MID, 0, 252);

  flash_timer = lv_timer_create(flash_cb, 400, NULL);
  lv_timer_pause(flash_timer);

  // Pre-allocate the alert-image buffer now, while the heap is still clean.
  s_alertCap = 196 * 196 * 2 + 8;               // largest 196px RGB565 hero + header
  s_alertBuf = (uint8_t *)malloc(s_alertCap);
  Serial.printf("[ui] alert img buffer: %s (%u bytes, free heap %u)\n",
                s_alertBuf ? "OK" : "FAILED", (unsigned)s_alertCap, (unsigned)ESP.getFreeHeap());
}

// ---------------------------------------------------------------------
void ui_update_clock(const char *hhmm, const char *ampm, const char *date) {
  lv_label_set_text(lbl_clock, hhmm);
  lv_label_set_text(lbl_ampm, ampm);
  lv_label_set_text(lbl_date, date);
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, -12, 40);
  lv_obj_align_to(lbl_ampm, lbl_clock, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -9);
}

void ui_update_next(const void *icon, const char *timestr, const char *name) {
  if (icon) { lv_obj_clear_flag(next_icon, LV_OBJ_FLAG_HIDDEN); lv_img_set_src(next_icon, icon); }
  else      { lv_obj_add_flag(next_icon, LV_OBJ_FLAG_HIDDEN); }   // text-only reminder
  lv_label_set_text(lbl_next_time, timestr);
  lv_label_set_text(lbl_next_name, name);
}

void ui_update_status(const char *text) {
  lv_label_set_text(lbl_status, text);
}

void ui_update_missed(int count) {
  if (count > 0) lv_label_set_text_fmt(lbl_missed, "%d missed", count);
  else           lv_label_set_text(lbl_missed, "");
}

// Top-left badge: shows when the user has any sleep/power-saving configured, plus the
// live USB status. On USB (grey) sleep is suppressed; on battery (amber) it's armed.
void ui_update_sleep_badge(bool configured, bool onUsb) {
  if (!configured) { lv_label_set_text(lbl_sleep, ""); return; }
  if (onUsb) {
    lv_obj_set_style_text_color(lbl_sleep, lv_color_hex(COL_DIM), 0);       // USB -> no sleep
    lv_label_set_text(lbl_sleep, "Zz\n" LV_SYMBOL_USB);                     // icon on the next line
  } else {
    lv_obj_set_style_text_color(lbl_sleep, lv_color_hex(0xF2C14E), 0);      // battery -> sleep armed
    lv_label_set_text(lbl_sleep, "Zz\n" LV_SYMBOL_BATTERY_2);
  }
}

// ---- boot summary overlay: shown for a few seconds at power-on ----
static lv_obj_t *boot_overlay = nullptr;
void ui_boot_summary(const char *text) {
  if (boot_overlay) return;
  boot_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(boot_overlay, LCD_W, LCD_H);
  lv_obj_align(boot_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(boot_overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(boot_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(boot_overlay, 0, 0);
  lv_obj_set_style_radius(boot_overlay, 0, 0);
  lv_obj_set_style_pad_all(boot_overlay, 12, 0);
  lv_obj_clear_flag(boot_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *l = lv_label_create(boot_overlay);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LCD_W - 24);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_move_foreground(boot_overlay);
}
void ui_boot_summary_hide() {
  if (boot_overlay) { lv_obj_del(boot_overlay); boot_overlay = nullptr; }
}

// ---- "going to sleep" notice, shown for a few seconds before deep sleep ----
static lv_obj_t *sleep_overlay = nullptr;
void ui_sleep_notice(const char *wakeText) {
  if (sleep_overlay) ui_hide_sleep_notice();
  sleep_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(sleep_overlay, LCD_W, LCD_H);
  lv_obj_align(sleep_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(sleep_overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(sleep_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sleep_overlay, 0, 0);
  lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *icon = lv_label_create(sleep_overlay);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_text(icon, "Zz");
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, -55);

  lv_obj_t *t1 = lv_label_create(sleep_overlay);
  lv_obj_set_style_text_font(t1, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t1, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(t1, "Going to sleep");
  lv_obj_align(t1, LV_ALIGN_CENTER, 0, 5);

  lv_obj_t *t2 = lv_label_create(sleep_overlay);
  lv_obj_set_style_text_font(t2, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(t2, lv_color_hex(COL_ACCENT), 0);
  lv_label_set_long_mode(t2, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t2, LCD_W - 20);
  lv_obj_set_style_text_align(t2, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(t2, wakeText);
  lv_obj_align(t2, LV_ALIGN_CENTER, 0, 45);

  lv_obj_move_foreground(sleep_overlay);
}
void ui_hide_sleep_notice() {
  if (sleep_overlay) { lv_obj_del(sleep_overlay); sleep_overlay = nullptr; }
}

// ---- wake-up mode prompt: silent "Awake?" screen until the user taps ----
static lv_obj_t *wake_overlay = nullptr;
void ui_wake_prompt(bool show) {
  if (!show) { if (wake_overlay) { lv_obj_del(wake_overlay); wake_overlay = nullptr; } return; }
  if (wake_overlay) return;
  wake_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(wake_overlay, LCD_W, LCD_H);
  lv_obj_align(wake_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(wake_overlay, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(wake_overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wake_overlay, 0, 0);
  lv_obj_clear_flag(wake_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ic = lv_label_create(wake_overlay);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(ic, lv_color_hex(0xF2C14E), 0);
  lv_label_set_text(ic, LV_SYMBOL_EYE_OPEN);
  lv_obj_align(ic, LV_ALIGN_CENTER, 0, -55);

  lv_obj_t *t1 = lv_label_create(wake_overlay);
  lv_obj_set_style_text_font(t1, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(t1, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(t1, "Awake?");
  lv_obj_align(t1, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *t2 = lv_label_create(wake_overlay);
  lv_obj_set_style_text_font(t2, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(t2, lv_color_hex(COL_DIM), 0);
  lv_label_set_long_mode(t2, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(t2, LCD_W - 24);
  lv_obj_set_style_text_align(t2, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(t2, "Tap the screen to start your day");
  lv_obj_align(t2, LV_ALIGN_CENTER, 0, 45);
  lv_obj_move_foreground(wake_overlay);
}

void ui_update_battery(int pct) {
  if (pct < 0) { lv_label_set_text(lbl_batt, ""); return; }
  const char *sym = pct >= 90 ? LV_SYMBOL_BATTERY_FULL
                  : pct >= 60 ? LV_SYMBOL_BATTERY_3
                  : pct >= 35 ? LV_SYMBOL_BATTERY_2
                  : pct >= 12 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  uint32_t col = pct >= 35 ? COL_DIM : (pct >= 12 ? 0xF2C14E : 0xFF6B6B);
  lv_obj_set_style_text_color(lbl_batt, lv_color_hex(col), 0);
  lv_label_set_text_fmt(lbl_batt, "%s %d%%", sym, pct);
}

// rock the alert icon left/right like a ringing bell
static void wiggle_exec(void *obj, int32_t v) {
  lv_img_set_angle((lv_obj_t *)obj, v);      // v in 0.1-degree units
}

void ui_icon_wiggle(bool on) {
  if (on) {
    lv_img_set_pivot(alert_icon, 98, 98);    // centre of the 196px icon
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, alert_icon);
    lv_anim_set_exec_cb(&a, wiggle_exec);
    lv_anim_set_values(&a, -160, 160);       // +/-16 degrees
    lv_anim_set_time(&a, 120);
    lv_anim_set_playback_time(&a, 120);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  } else {
    lv_anim_del(alert_icon, wiggle_exec);
    lv_img_set_angle(alert_icon, 0);
  }
}

// If `icon` is an "L:/..." file path, read the whole .bin into RAM and return an
// in-memory descriptor (the same code path the built-in icons use). Falls back to
// the original pointer on any error. The previous buffer is freed each call.
static const void *alert_icon_ram(const void *icon) {
  const char *p = (const char *)icon;
  bool isFile = (p && p[0] == 'L' && p[1] == ':');
  if (!isFile) return icon;                          // built-in dsc -> use as-is
  if (!s_alertBuf) { Serial.println("[ui] no alert buffer -> file fallback"); return icon; }
  File f = LittleFS.open(p + 2, "r");                // skip the "L:" prefix
  if (!f) { Serial.printf("[ui] alert img open FAIL %s\n", p + 2); return icon; }
  size_t sz = f.size();
  if (sz < 5 || sz > s_alertCap) { Serial.printf("[ui] alert img bad size %u\n", (unsigned)sz); f.close(); return icon; }
  size_t rd = f.read(s_alertBuf, sz);                // read straight into the pre-allocated buffer
  f.close();
  if (rd != sz) { Serial.printf("[ui] alert img short read %u/%u\n", (unsigned)rd, (unsigned)sz); return icon; }
  uint32_t h = s_alertBuf[0] | (s_alertBuf[1] << 8) | (s_alertBuf[2] << 16) | ((uint32_t)s_alertBuf[3] << 24);
  memset(&s_alertDsc, 0, sizeof(s_alertDsc));
  s_alertDsc.header.cf = h & 0x1F;
  s_alertDsc.header.w  = (h >> 10) & 0x7FF;
  s_alertDsc.header.h  = (h >> 21) & 0x7FF;
  s_alertDsc.data      = s_alertBuf + 4;
  s_alertDsc.data_size = sz - 4;
  Serial.printf("[ui] alert img %s  cf=%u %ux%u data=%u\n", p + 2,
                s_alertDsc.header.cf, s_alertDsc.header.w, s_alertDsc.header.h, (unsigned)(sz - 4));
  return &s_alertDsc;
}

// ---------------------------------------------------------------------
void ui_show_alert(const void *icon, const char *pill,
                   const char *timestr, const char *name, uint32_t accent) {
  flash_accent = accent;
  lv_label_set_text(alert_pill, pill);

  if (icon) {
    // ---- with image: big icon on top, smaller text below ----
    icon = alert_icon_ram(icon);                     // load file images into RAM first
    lv_obj_clear_flag(alert_icon, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(alert_icon, icon);
    lv_obj_align(alert_icon, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_text_font(alert_time, &lv_font_montserrat_20, 0);
    lv_obj_align(alert_time, LV_ALIGN_TOP_MID, 0, 226);
    lv_obj_set_style_text_font(alert_name, &lv_font_montserrat_16, 0);
    lv_obj_set_width(alert_name, 228);
    lv_obj_align(alert_name, LV_ALIGN_TOP_MID, 0, 252);
  } else {
    // ---- no image: fill the freed space with large, easy-to-read text ----
    lv_obj_add_flag(alert_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(alert_time, &lv_font_montserrat_28, 0);
    lv_obj_align(alert_time, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_font(alert_name, &lv_font_montserrat_28, 0);
    lv_obj_set_width(alert_name, 232);
    lv_obj_align(alert_name, LV_ALIGN_CENTER, 0, 20);
  }

  lv_label_set_text(alert_time, timestr);
  lv_label_set_text(alert_name, name);
  lv_obj_set_style_text_color(alert_time, lv_color_hex(accent ? accent : COL_ACCENT), 0);
  lv_obj_set_style_border_color(alert_layer, lv_color_hex(accent ? accent : COL_ACCENT), 0);
  lv_obj_clear_flag(alert_layer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(alert_layer);
  flash_ticks = 0;
  lv_timer_resume(flash_timer);
}

void ui_hide_alert() {
  ui_icon_wiggle(false);                  // stop the bell wiggle + reset angle
  lv_obj_add_flag(alert_layer, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(flash_timer);
}

bool ui_alert_active() {
  return !lv_obj_has_flag(alert_layer, LV_OBJ_FLAG_HIDDEN);
}
