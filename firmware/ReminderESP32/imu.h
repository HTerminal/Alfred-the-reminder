#pragma once

// Tap-to-dismiss via the onboard QMI8658 IMU (no touchscreen on this board).
// I2C (Wire) must already be running on the given pins.

bool  imu_begin(int sda, int scl);  // true if the QMI8658 was found
void  imu_arm();                    // call when an alert starts (resets baseline)
void  imu_service();                // sample the accel — call EVERY loop
bool  imu_tapped();                 // true once per detected tap
float imu_peak_jerk();              // peak jerk (g) since last call — for the web debug readout
