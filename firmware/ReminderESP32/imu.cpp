// Tap detection on the QMI8658 (SensorLib). We read the accelerometer and
// look for a sharp jerk (sudden change in |a|) — a deliberate finger tap
// spikes well above gravity, while speaker vibration stays tiny.
#include "imu.h"
#include "config.h"
#include "schedule.h"      // g_tapThreshold (web-adjustable)
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <SensorQMI8658.hpp>

static SensorQMI8658 qmi;
static bool     ready       = false;
static float    prevMag     = 1.0f;
static uint32_t lastTap     = 0;
static uint32_t armedAt     = 0;
static volatile bool  s_tapPending = false;
static volatile float s_peakJerk   = 0.0f;

bool imu_begin(int sda, int scl) {
  if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, sda, scl)) {
    Serial.println("[imu] QMI8658 not found");
    return false;
  }
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G,
                          SensorQMI8658::ACC_ODR_1000Hz,
                          SensorQMI8658::LPF_MODE_0);
  qmi.enableAccelerometer();
  ready = true;
  Serial.println("[imu] QMI8658 ready (tap-to-dismiss)");
  return true;
}

void imu_arm() {
  armedAt = millis();
  prevMag = 1.0f;
  s_tapPending = false;
}

// Sample the accelerometer every loop. Tracks the peak jerk (for the web debug
// readout) and flags a tap when the jerk exceeds the (web-set) threshold.
void imu_service() {
  if (!ready) return;
  float x, y, z;
  if (!qmi.getAccelerometer(x, y, z)) return;          // values in g
  float mag   = sqrtf(x * x + y * y + z * z);
  float delta = fabsf(mag - prevMag);
  prevMag = mag;
  if (delta > s_peakJerk) s_peakJerk = delta;

  uint32_t now = millis();
  if (now - armedAt < TAP_SETTLE_MS) return;            // ignore just-armed transient
  // Never trust a threshold under the sensor's own noise floor — a stale config
  // could still hold one, and it would turn idle noise into a stream of phantom
  // taps that silently dismiss every alert.
  float thr = g_tapThreshold < TAP_MIN_G ? TAP_MIN_G : g_tapThreshold;
  if (delta > thr && (now - lastTap) > TAP_DEBOUNCE_MS) {
    lastTap = now;
    s_tapPending = true;
    Serial.printf("[imu] tap %.2fg (thr %.2f)\r\n", delta, thr);
  }
}

bool imu_tapped() {
  if (s_tapPending) { s_tapPending = false; return true; }
  return false;
}

float imu_peak_jerk() { float j = s_peakJerk; s_peakJerk = 0; return j; }
