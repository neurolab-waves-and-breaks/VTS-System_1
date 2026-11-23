#ifndef VTS_TELEMETRY_SNAPSHOT_H
#define VTS_TELEMETRY_SNAPSHOT_H
#include "vts_phase_link.h"
#include "vts_telemetry_packet.h"
/* Pure, read-only access: neither function changes the link or timer. */
bool vts_telemetry_snapshot(const vts_phase_link_t *link, uint64_t now,
                            uint16_t sequence, vts_telemetry_packet_t *out);
bool vts_telemetry_window_allowed(const vts_phase_link_t *link, uint64_t now,
                                  uint32_t window_ms);
#endif
