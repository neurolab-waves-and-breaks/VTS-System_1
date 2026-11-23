#ifndef VTS_TELEMETRY_PACKET_H
#define VTS_TELEMETRY_PACKET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Protocol v13: fixed 24-byte manufacturer payload; no extra RF windows.
 * Byte 2: high nibble is Fine parent (1..12) or zero for Coarse;
 * low nibble is 1-based current member (1..7 fine, 1..12 coarse).
 * All masks describe the ACTIVE ring; elapsed/impedance/CRC unchanged.
 * OLD Android v1.12d WILL REJECT THIS VERSION; do not flash this experimental
 * firmware with an unprepared v12-only patient display.
 */
#define VTS_TELEMETRY_VERSION 13U
#define VTS_TELEMETRY_BYTES 24U
#define VTS_TELEMETRY_COMPANY_ID 0xffffU
#define VTS_TF_PERMANENT 0x01U
#define VTS_TF_PENDING   0x02U
#define VTS_TF_ACTIVE    0x04U
#define VTS_TF_CONFLICT  0x08U
#define VTS_TF_FAULT     0x10U
#define VTS_TF_STARTUP   0x20U
#define VTS_TF_FINE      0x40U
#define VTS_TF_ALL       0x7fU
#define VTS_TELEMETRY_NA UINT32_MAX
#define VTS_TELEMETRY_PHASE_NA INT16_MAX

typedef struct {
    uint8_t flags, index_one_based, impedance_state;
    uint16_t sequence;
    int16_t phase_deg;
    uint16_t rejected_mask, tested_mask, permanent_mask;
    uint32_t elapsed_seconds, impedance_kohm;
    uint8_t fine_parent_one_based; /* 0 Coarse; 1..12 Fine */
} vts_telemetry_packet_t;
uint16_t vts_telemetry_crc16(const uint8_t *data,size_t length);
bool vts_telemetry_encode(uint8_t out[VTS_TELEMETRY_BYTES],const vts_telemetry_packet_t *s);
bool vts_telemetry_decode(vts_telemetry_packet_t *out,const uint8_t *data,size_t length);
#endif
