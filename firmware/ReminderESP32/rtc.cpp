// PCF85063 RTC driver (Waveshare onboard clock chip).
// Registers: 0x04 sec, 0x05 min, 0x06 hour, 0x07 day, 0x08 weekday,
//            0x09 month, 0x0A year (base 2000). Sec bit7 = osc-stop flag.
#include "rtc.h"
#include <Wire.h>

static uint8_t s_addr = 0x51;

static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

static bool present() {
  Wire.beginTransmission(s_addr);
  return Wire.endTransmission() == 0;
}

bool rtc_begin(uint8_t addr) {
  s_addr = addr;
  return present();
}

bool rtc_time_valid() {
  Wire.beginTransmission(s_addr);
  Wire.write(0x04);                      // seconds register
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)s_addr, 1) != 1) return false;
  uint8_t sec = Wire.read();
  return (sec & 0x80) == 0;              // bit7 set -> clock integrity lost
}

bool rtc_get(struct tm *out) {
  Wire.beginTransmission(s_addr);
  Wire.write(0x04);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)s_addr, 7) != 7) return false;
  uint8_t sec  = Wire.read();
  uint8_t mins = Wire.read();
  uint8_t hrs  = Wire.read();
  uint8_t day  = Wire.read();
  uint8_t wday = Wire.read();
  uint8_t mon  = Wire.read();
  uint8_t yr   = Wire.read();

  out->tm_sec  = bcd2dec(sec & 0x7F);
  out->tm_min  = bcd2dec(mins & 0x7F);
  out->tm_hour = bcd2dec(hrs & 0x3F);    // 24-hour mode
  out->tm_mday = bcd2dec(day & 0x3F);
  out->tm_wday = wday & 0x07;
  out->tm_mon  = bcd2dec(mon & 0x1F) - 1;
  out->tm_year = bcd2dec(yr) + 100;      // years since 1900 (2000 -> 100)
  out->tm_isdst = 0;
  return true;
}

bool rtc_set(const struct tm *in) {
  Wire.beginTransmission(s_addr);
  Wire.write(0x04);
  Wire.write(dec2bcd(in->tm_sec) & 0x7F);   // also clears the osc-stop flag
  Wire.write(dec2bcd(in->tm_min));
  Wire.write(dec2bcd(in->tm_hour));
  Wire.write(dec2bcd(in->tm_mday));
  Wire.write(in->tm_wday & 0x07);
  Wire.write(dec2bcd(in->tm_mon + 1));
  Wire.write(dec2bcd((in->tm_year - 100) & 0xFF));
  return Wire.endTransmission() == 0;
}
