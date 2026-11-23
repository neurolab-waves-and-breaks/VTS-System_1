#include "ble_packet.h"
#include <stddef.h>
#include <string.h>

static void write16(uint8_t *out, uint16_t x)
{
    out[0] = (uint8_t)x;
    out[1] = (uint8_t)(x >> 8);
}

static void write32(uint8_t *out, uint32_t x)
{
    for (unsigned i = 0; i < 4U; ++i) {
        out[i] = (uint8_t)(x >> (8U * i));
    }
}

uint16_t vts_ble_packet_crc16(const uint8_t *bytes, unsigned len)
{
    uint16_t crc = 0xffffU;
    for (unsigned i = 0; i < len; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (unsigned j = 0; j < 8U; ++j) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

bool vts_ble_packet_encode(uint8_t out[VTS_BLE_PACKET_SIZE],
                           const struct vts_ble_phase_snapshot *snapshot,
                           uint32_t sequence, uint32_t uptime_ms)
{
    if (out == NULL || snapshot == NULL ||
        snapshot->index_one_based == 0U ||
        snapshot->index_one_based > VTS_BLE_PHASE_COUNT ||
        snapshot->phase_deg < -180 || snapshot->phase_deg > 180 ||
        (snapshot->rejected_mask & 0xf000U) != 0U ||
        (snapshot->accepted_mask & 0xf000U) != 0U) {
        return false;
    }
    memset(out, 0, VTS_BLE_PACKET_SIZE);
    out[0] = VTS_BLE_PACKET_VERSION;
    out[1] = (snapshot->locked ? VTS_BLE_FLAG_LOCKED : 0U) |
             (snapshot->pending ? VTS_BLE_FLAG_PENDING : 0U);
    out[2] = VTS_BLE_PHASE_COUNT;
    out[3] = snapshot->index_one_based;
    write32(out + 4, sequence);
    write32(out + 8, uptime_ms);
    write16(out + 12, snapshot->rejected_mask);
    write16(out + 14, snapshot->accepted_mask);
    write16(out + 16, (uint16_t)snapshot->phase_deg);
    write16(out + 18, vts_ble_packet_crc16(out, 18U));
    return true;
}
