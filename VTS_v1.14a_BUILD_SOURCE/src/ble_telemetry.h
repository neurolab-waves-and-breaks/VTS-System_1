#ifndef VTS_BLE_TELEMETRY_H
#define VTS_BLE_TELEMETRY_H
#include "vts_telemetry_packet.h"
#include <stdint.h>
#include <stdbool.h>
/* All functions are called by main (the existing single ESB owner), never
 * from an ISR or system-workqueue callback. No commands from the phone. */
int vts_ble_telemetry_init(void);
bool vts_ble_telemetry_busy(void);
bool vts_ble_telemetry_failed(void);
bool vts_ble_telemetry_due(uint64_t now_ms);
/* Request a prompt post-commit snapshot. Only the main ESB owner services it;
 * a request never interrupts an in-flight CLIC command or BLE window. */
void vts_ble_telemetry_request_priority(void);
/* Two guarded snapshots spaced ~850 ms after confirmed startup or restore.
 * They corroborate a reset sequence counter without altering v12 payloads. */
void vts_ble_telemetry_request_startup_sync(void);
/* Finish a window when due, or early for a queued input/button/fault. */
void vts_ble_telemetry_service(bool abort_window, uint64_t now_ms);
/* recheck runs after ESB stops, to close the last IRQ/guard race. */
void vts_ble_telemetry_poll(const vts_telemetry_packet_t *snapshot,
                           bool allowed, uint64_t now_ms, bool (*recheck)(void));
#endif
