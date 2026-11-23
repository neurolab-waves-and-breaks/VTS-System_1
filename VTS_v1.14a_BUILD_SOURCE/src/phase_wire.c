#include "phase_wire.h"

bool vts_pack_set_phase(int32_t degrees, uint8_t body[5])
{
    if (body == 0 || degrees < -180 || degrees > 180) return false;
    uint32_t v = (uint32_t)degrees;
    body[0] = 30;
    body[1] = (uint8_t)(v >> 24);
    body[2] = (uint8_t)(v >> 16);
    body[3] = (uint8_t)(v >> 8);
    body[4] = (uint8_t)v;
    return true;
}
void vts_pack_get_phase(uint8_t body[5])
{
    if (body == 0) return;
    body[0] = 29;
    body[1] = 0; body[2] = 0; body[3] = 0; body[4] = 0;
}
int32_t vts_decode_phase_parameter(uint32_t raw)
{
    /* Sign-convert without implementation-defined out-of-range unsigned cast. */
    int64_t signed_value = (raw & UINT32_C(0x80000000)) ?
        ((int64_t)raw - INT64_C(4294967296)) : (int64_t)raw;
    return (int32_t)signed_value;
}
