#pragma once
/**
 * ota_update.h — Over-the-air firmware update from GitHub Releases
 *
 * Spawned as a FreeRTOS task when the user taps "Firmware Upgrade"
 * on the on-screen WiFi config page.
 *
 * Fetches the latest release from VID-PRO/BambuTagger-Console,
 * downloads firmware.bin, and flashes it via the Update class.
 */

void ota_task(void *param);
