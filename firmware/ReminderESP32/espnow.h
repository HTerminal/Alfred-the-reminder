#pragma once

// ESP-NOW receiver for the doorbell button. WiFi (STA) must be started first.
bool espnow_begin();
bool espnow_ring_pending();   // true once when a valid "ring" broadcast arrives
