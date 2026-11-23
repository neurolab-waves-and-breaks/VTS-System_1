#ifndef VTS_BLE_PACKET_H
#define VTS_BLE_PACKET_H

#include <stdint.h>
#include <stdbool.h>

#define VTS_BLE_PACKET_VERSION 9U
#define VTS_BLE_PACKET_SIZE 20U
#define VTS_BLE_PHASE_COUNT 12U
#define VTS_BLE_FLAG_LOCKED 0x01U
#define VTS_BLE_FLAG_PENDING 0x02U

/* Packet v9, little-endian integers, 20 bytes:
 * [0] protocol 9; [1] flags; [2] count=12; [3] index=1..12
 * [4..7] sequence uint32; [8..11] VTS uptime ms uint32
 * [12..13] rejected history mask u16; [14..15] accepted history mask u16
 * [16..17] confirmed phase signed i16, degrees; [18..19] CRC16-CCITT-FALSE.
 * This is not the colleague's v6 simulated phase protocol.
 */
struct vts_ble_phase_snapshot {
    int16_t phase_deg;
    uint8_t index_one_based;
    uint16_t rejected_mask;
    uint16_t accepted_mask;
    bool locked;
    bool pending;
};

uint16_t vts_ble_packet_crc16(const uint8_t *bytes, unsigned len);
bool vts_ble_packet_encode(uint8_t out[VTS_BLE_PACKET_SIZE],
                           const struct vts_ble_phase_snapshot *snapshot,
                           uint32_t sequence, uint32_t uptime_ms);

#endif
