// =====================================================================
//  DoorbellButton  -  Seeed XIAO ESP32-C3 doorbell button  (DEEP SLEEP)
//  Press the button -> broadcasts a "ring" over ESP-NOW to the display,
//  stays awake 3s, then DEEP-SLEEPS (microamps) until the next press.
//
//    * Wake button : GPIO4  (= the "D2" pad on the XIAO ESP32-C3)
//                    ACTIVE-HIGH -> wire the button  D2 <-> 3V3.
//                    Internal pull-down holds it LOW when idle.
//
//  IMPORTANT - why GPIO4 and not D4/BOOT:
//    On the ESP32-C3, DEEP sleep can ONLY be woken by GPIO0-GPIO5 (the RTC
//    pins). D4 = GPIO6 and BOOT = GPIO9 are outside that range, so deep
//    sleep can never wake on them. GPIO4 (the D2 pad) IS wake-capable, so
//    the button lives there. The onboard BOOT button cannot wake deep
//    sleep - if you need BOTH buttons, use the light-sleep version instead.
//    (Any of D0/D1/D2/D3 = GPIO2/3/4/5 also work - change WAKE_GPIO below.)
//
//  Flash with:  esp32:esp32:XIAO_ESP32C3   (USB-CDC serial enabled)
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#define WAKE_GPIO       4             // GPIO4 = "D2" pad on the XIAO C3; deep-sleep-wake capable, ACTIVE-HIGH
#define DOORBELL_MAGIC  0xB311BE11UL  // must match the display's config.h
#define AWAKE_MS        3000          // stay awake 3s after a press before the sleep message
#define PRESLEEP_MS     1000          // ...then sleep 1s after the sleep message

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t  cmd;      // 1 = ring
} doorbell_msg_t;

static uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void radioBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) { Serial.println("[doorbell] ESP-NOW init FAILED"); return; }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST, 6);
  peer.channel = 0; peer.encrypt = false;
  esp_now_add_peer(&peer);
}

static void radioEnd() {
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);       // radio fully off before we sleep
}

static void sendRing() {
  doorbell_msg_t msg = { DOORBELL_MAGIC, 1 };
  for (int ch = 1; ch <= 13; ch++) {         // broadcast on every channel so it always lands
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    delay(5);
    esp_now_send(BROADCAST, (const uint8_t *)&msg, sizeof(msg));
    delay(5);
  }
}

static void deepSleep() {
  pinMode(WAKE_GPIO, INPUT_PULLDOWN);                 // idle LOW; a press pulls it HIGH
  gpio_pulldown_en((gpio_num_t)WAKE_GPIO);            // keep the pull-down alive through deep sleep
  gpio_pullup_dis((gpio_num_t)WAKE_GPIO);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_GPIO, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();                             // never returns; the chip resets on wake
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("[doorbell] XIAO ESP32-C3 button - boot");

  // On the C3, a deep-sleep GPIO wake reports ESP_SLEEP_WAKEUP_GPIO.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("[doorbell] woke by button press -> RING");
    radioBegin();
    sendRing();
    radioEnd();
    Serial.println("[doorbell] ring sent");
  } else {
    Serial.println("[doorbell] cold boot (power-on) - no ring");
  }

  Serial.printf("[doorbell] staying awake %d ms...\n", AWAKE_MS);
  delay(AWAKE_MS);                                    // wait 3 seconds after the click

  Serial.println("[doorbell] going to sleep");        // print the sleep message AFTER the 3s
  delay(PRESLEEP_MS);                                 // ...then actually sleep 1 second later
  Serial.println("[doorbell] deep sleeping now - press the button to wake");
  Serial.flush();
  deepSleep();
}

void loop() {
  // never runs: setup() handles the press and deep-sleeps; wake restarts setup().
}
