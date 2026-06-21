#pragma once
/**
 * ota_update.h — Over-the-air firmware update from GitHub Releases
 *
 * Spawned as a FreeRTOS task when the user taps "Firmware Upgrade"
 * on the on-screen WiFi config page.
 *
 * Fetches the latest release from VID-PRO/BambuTagger-Console,
 * downloads BambuTagger-Console.ino.bin, and flashes it via the Update class.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void ota_task(void *param);

/** Non-null while an OTA task is running; set/cleared by ota_task. */
extern TaskHandle_t g_ota_task_handle;

/** Returns true if an OTA-update task is currently executing. */
inline bool ota_is_busy() { return g_ota_task_handle != nullptr; }
