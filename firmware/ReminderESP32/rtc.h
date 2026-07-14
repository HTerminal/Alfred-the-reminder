#pragma once
#include <time.h>

// Minimal PCF85063 (Waveshare onboard RTC) driver over the global `Wire`.
// Wire.begin(sda,scl) must already have been called (see i2c auto-detect).

bool rtc_begin(uint8_t addr);      // returns true if the chip answers
bool rtc_time_valid();             // false if the oscillator-stop flag is set
bool rtc_get(struct tm *out);      // read local time from the RTC
bool rtc_set(const struct tm *in); // write local time to the RTC
