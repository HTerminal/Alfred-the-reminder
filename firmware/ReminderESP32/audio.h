#pragma once

// Chime playback through the onboard ES8311 codec + speaker.
// Wire.begin(sda,scl) must already have run (I2C auto-detect in the .ino).

bool audio_begin();   // init I2S + ES8311; false if codec/I2S unavailable
bool audio_ready();
void audio_chime();     // fallback reminder chime (generated)
void audio_dingdong();  // fallback doorbell (generated)

// --- WAV file playback from LittleFS (16 kHz mono 16-bit), non-blocking ---
bool audio_play_file(const char *path);       // start; false if missing/bad
bool audio_play_seq(const char *a, const char *b); // play a, then b (chime, then voice)
void audio_service();                    // call every loop to push audio
bool audio_busy();                       // true while a file is playing
void audio_stop();                       // stop current playback
