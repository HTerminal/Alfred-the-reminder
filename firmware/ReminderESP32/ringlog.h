#pragma once
#include <stdint.h>

// A small, persisted history of the last 100 doorbell rings (epoch timestamps).
// Stored on LittleFS so it survives reboots and deep-sleep wakes. Shown only on
// the web config page (see webconfig.cpp /api/ringlog).
void     ringlog_begin();            // load the log from LittleFS
void     ringlog_add(uint32_t epoch); // record a ring at `epoch` and persist
int      ringlog_count();            // number of entries (0..100)
uint32_t ringlog_at(int i);          // i = 0 (oldest) .. count-1 (newest); 0 if out of range
