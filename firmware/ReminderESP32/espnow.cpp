// ESP-NOW receiver: listens for the doorbell button's broadcast "ring".
// Shares the radio with WiFi STA (uses the current channel).
#include "espnow.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

typedef struct __attribute__((packed)) {
  uint32_t magic;   // must equal DOORBELL_MAGIC
  uint8_t  cmd;     // 1 = ring
} doorbell_msg_t;

static volatile bool ringFlag = false;

// ESP32 Arduino core 3.x recv-callback signature
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  if (len >= (int)sizeof(doorbell_msg_t)) {
    const doorbell_msg_t *m = (const doorbell_msg_t *)data;
    if (m->magic == DOORBELL_MAGIC && m->cmd == 1) ringFlag = true;
  }
}

bool espnow_begin() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("[espnow] init failed");
    return false;
  }
  esp_now_register_recv_cb(onRecv);
  Serial.print("[espnow] ready, MAC ");
  Serial.println(WiFi.macAddress());
  return true;
}

bool espnow_ring_pending() {
  if (ringFlag) { ringFlag = false; return true; }
  return false;
}
