#include "ringlog.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#define RINGLOG_MAX  100
#define RINGLOG_PATH "/ringlog.bin"       // [uint16 count][uint32 epoch] * count

static uint32_t s_ts[RINGLOG_MAX];
static int      s_n = 0;

void ringlog_begin() {
  s_n = 0;
  File f = LittleFS.open(RINGLOG_PATH, "r");
  if (!f) return;
  uint16_t n = 0;
  if (f.read((uint8_t *)&n, 2) == 2 && n <= RINGLOG_MAX) {
    int rd = f.read((uint8_t *)s_ts, (size_t)n * 4);
    s_n = (rd > 0) ? rd / 4 : 0;
  }
  f.close();
  Serial.printf("[ringlog] loaded %d entries\n", s_n);
}

static void ringlog_save() {
  File f = LittleFS.open(RINGLOG_PATH, "w");
  if (!f) return;
  uint16_t n = (uint16_t)s_n;
  f.write((uint8_t *)&n, 2);
  f.write((uint8_t *)s_ts, (size_t)s_n * 4);
  f.close();
}

void ringlog_add(uint32_t epoch) {
  if (s_n >= RINGLOG_MAX) {                          // full -> drop the oldest
    memmove(s_ts, s_ts + 1, (RINGLOG_MAX - 1) * sizeof(uint32_t));
    s_n = RINGLOG_MAX - 1;
  }
  s_ts[s_n++] = epoch;
  ringlog_save();
}

int      ringlog_count()      { return s_n; }
uint32_t ringlog_at(int i)    { return (i >= 0 && i < s_n) ? s_ts[i] : 0; }
