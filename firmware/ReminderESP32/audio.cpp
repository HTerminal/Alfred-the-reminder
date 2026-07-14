// =====================================================================
//  audio.cpp  ·  chime via the onboard ES8311 codec + speaker
//  Uses Waveshare's tested ES8311 driver (src/es8311/) + STEREO I2S,
//  matching their 15_ES8311 example. I2C must already be up on port 0
//  (the i2c auto-detect in the .ino calls Wire.begin(8,7) first).
// =====================================================================
#include "audio.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include <math.h>
#include "src/es8311/es8311.h"

static I2SClass       i2s;
static bool           ready = false;
static es8311_handle_t es   = nullptr;

// ---- WAV streaming: runs in its OWN task so LVGL/wiggle can't starve it ----
static volatile bool reqPlay     = false;
static volatile bool stopReq     = false;
static volatile bool taskPlaying = false;
static char          reqPath[80]  = {0};   // first file (e.g. the chime)
static char          reqPath2[80] = {0};   // second file, played right after (the voice)
static TaskHandle_t  audioTask    = nullptr;
static void audioTaskFn(void *);   // defined below, used in audio_begin

bool audio_begin() {
#if !ENABLE_CHIME
  return false;
#else
  if (PA_ENABLE_PIN >= 0) { pinMode(PA_ENABLE_PIN, OUTPUT); digitalWrite(PA_ENABLE_PIN, HIGH); }

  // I2S: STEREO, 16-bit, 16 kHz, MCLK auto-generated (256*fs) on the MCLK pin
  i2s.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  if (!i2s.begin(I2S_MODE_STD, AUDIO_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("[audio] I2S begin failed");
    return false;
  }

  // ES8311 codec over I2C port 0 (already initialised by the i2c auto-detect)
  es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) { Serial.println("[audio] es8311_create failed"); return false; }

  es8311_clock_config_t clk;
  clk.mclk_inverted     = false;
  clk.sclk_inverted     = false;
  clk.mclk_from_mclk_pin = true;
  clk.mclk_frequency    = AUDIO_RATE * 256;
  clk.sample_frequency  = AUDIO_RATE;

  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("[audio] es8311_init failed");
    return false;
  }
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_microphone_config(es, false);
  es8311_voice_volume_set(es, CHIME_VOLUME, NULL);

  ready = true;
  xTaskCreatePinnedToCore(audioTaskFn, "audio", 4096, nullptr, 3, &audioTask, 0);
  Serial.println("[audio] ES8311 ready (stereo) + streaming task");
  return true;
#endif
}

bool audio_ready() { return ready; }

// A struck chime "bell": fundamental + soft harmonics, quick attack, then an
// exponential decay (rings and fades) — this is what makes it sound like a
// real doorbell instead of a buzzer. Streamed in small chunks (any length).
static void play_bell(float freq, int ms, float amp) {
  if (!ready) return;
  const int sr = 16000;
  const int total = sr * ms / 1000;
  const float tau = (ms / 1000.0f) / 3.5f;      // decay time constant
  const int   CHUNK = 256;
  static int16_t buf[CHUNK * 2];                // stereo interleaved, ~1 KB

  int i = 0;
  while (i < total) {
    int c = (total - i) < CHUNK ? (total - i) : CHUNK;
    for (int k = 0; k < c; k++) {
      float t   = (float)(i + k) / sr;
      float env = expf(-t / tau);               // ring-out
      if (i + k < 48) env *= (float)(i + k) / 48.0f;   // 3 ms soft attack (no click)
      float w = 2.0f * (float)M_PI * freq * t;
      float s = (sinf(w) + 0.45f * sinf(2.0f * w) + 0.22f * sinf(3.0f * w)) / 1.67f;
      int16_t v = (int16_t)(s * env * amp * 32767.0f);
      buf[2 * k] = v; buf[2 * k + 1] = v;
    }
    i2s.write((uint8_t *)buf, c * 2 * sizeof(int16_t));
    i += c;
  }
}

// Reminder: one gentle chime "ding" (repeated to form the ring).
void audio_chime() {
  if (!ready) return;
  play_bell(1046.5f, 420, 0.85f);         // C6, rings and fades
}

// Doorbell: the classic warm two-note "ding ... dong".
void audio_dingdong() {
  if (!ready) return;
  play_bell(659.25f, 520, 0.90f);         // E5  "ding"
  play_bell(523.25f, 780, 0.90f);         // C5  "dong" (lower, longer ring)
}

// ---------------- WAV file playback (16 kHz mono 16-bit) ----------------
// The audio task: opens the requested WAV, finds the PCM, and streams it to
// I2S continuously. Because it lives in its own task, i2s.write() paces it and
// the main loop (LVGL, wiggle, tap) runs in the gaps — no underruns, no glitches.
// Stream one WAV file to I2S (blocks until finished or stopReq).
static void streamOne(const char *path) {
  static int16_t mono[512];
  static int16_t stereo[1024];
  File f = LittleFS.open(path, "r");
  if (!f) return;

  char tag[4]; uint32_t len = 0, remaining = 0;
  bool ok = (f.read((uint8_t *)tag, 4) == 4 && memcmp(tag, "RIFF", 4) == 0);
  if (ok) { f.read((uint8_t *)&len, 4); ok = (f.read((uint8_t *)tag, 4) == 4 && memcmp(tag, "WAVE", 4) == 0); }
  while (ok && f.available() >= 8) {
    f.read((uint8_t *)tag, 4); f.read((uint8_t *)&len, 4);
    if (memcmp(tag, "data", 4) == 0) { remaining = len; break; }
    f.seek(f.position() + len);
  }
  while (remaining > 0 && !stopReq) {
    uint32_t want = remaining < sizeof(mono) ? remaining : sizeof(mono);
    int n = f.read((uint8_t *)mono, want);
    if (n <= 0) break;
    int samples = n / 2;
    for (int i = 0; i < samples; i++) { stereo[2 * i] = mono[i]; stereo[2 * i + 1] = mono[i]; }
    i2s.write((uint8_t *)stereo, samples * 2 * sizeof(int16_t));
    remaining -= n;
  }
  f.close();
}

static void audioTaskFn(void *) {
  for (;;) {
    if (!reqPlay) { vTaskDelay(pdMS_TO_TICKS(4)); continue; }
    reqPlay = false;
    stopReq = false;
    taskPlaying = true;
    if (reqPath[0])              streamOne(reqPath);    // e.g. the chime
    if (!stopReq && reqPath2[0]) streamOne(reqPath2);   // then the voice
    taskPlaying = false;
  }
}

void audio_stop() {
  stopReq = true;
  uint32_t t0 = millis();
  while (taskPlaying && millis() - t0 < 120) vTaskDelay(pdMS_TO_TICKS(2));
  reqPlay = false;
}

bool audio_busy() { return taskPlaying || reqPlay; }

// Hand a file to the audio task; false (so the caller can fall back to a chime)
// if the file isn't on the filesystem.
bool audio_play_file(const char *path) {
  if (!ready) return false;
  if (!LittleFS.exists(path)) { Serial.printf("[audio] missing %s\n", path); return false; }
  audio_stop();
  strncpy(reqPath, path, sizeof(reqPath) - 1); reqPath[sizeof(reqPath) - 1] = 0;
  reqPath2[0] = 0;
  reqPlay = true;
  return true;
}

// Play file `a` then file `b` back-to-back (e.g. chime, then the voice prompt).
bool audio_play_seq(const char *a, const char *b) {
  if (!ready) return false;
  if (!LittleFS.exists(a)) return audio_play_file(b);   // no chime -> just the voice
  audio_stop();
  strncpy(reqPath,  a,          sizeof(reqPath) - 1);  reqPath[sizeof(reqPath) - 1]  = 0;
  strncpy(reqPath2, b ? b : "", sizeof(reqPath2) - 1); reqPath2[sizeof(reqPath2) - 1] = 0;
  reqPlay = true;
  return true;
}

void audio_service() { /* streaming happens in audioTaskFn now */ }
