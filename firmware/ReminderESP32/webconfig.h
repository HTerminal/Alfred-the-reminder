#pragma once

void webconfig_begin();          // start the HTTP server (call after WiFi connects)
void webconfig_loop();           // call every loop
bool webconfig_schedule_dirty(); // true once after the schedule was edited on the web
bool webconfig_test_pending();   // true once after the "Test alert" button was pressed
bool webconfig_sleep_pending();  // true once after the "Deep sleep now" button was pressed
bool webconfig_reboot_pending(); // true once after the "Reboot" button was pressed
bool webconfig_wifireset_pending(); // true once after the "Reset Wi-Fi" button was pressed
