#ifndef VTS_PHASE_WIRE_H
#define VTS_PHASE_WIRE_H
#include <stdbool.h>
#include <stdint.h>
/* Pure protocol helpers: 5-byte CLIC body, signed big-endian degrees. */
bool vts_pack_set_phase(int32_t degrees, uint8_t body[5]);
void vts_pack_get_phase(uint8_t body[5]);
int32_t vts_decode_phase_parameter(uint32_t be_raw);
#endif
