#ifndef VTS_PHASE_PERSIST_H
#define VTS_PHASE_PERSIST_H
/* Original key 0x5301 retains VTSP header. Legacy v1 records = 24 bytes.
 * Experimental v2 records = 36 bytes (fine context + coarse context + CRC32).
 * A v1.13f downgrade MUST fail closed on a present v2 record, never restore
 * a stale previous Permanent phase; see docs/ROLLBACK_NVS.md.
 */
#include "vts_phase_fsm.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define VTS_PERSIST_SIZE 24U
#define VTS_PERSIST_V2_SIZE 36U
#define VTS_PERSIST_STORAGE_SIZE VTS_PERSIST_V2_SIZE

typedef struct {
    uint8_t current_index;  /* original coarse pointer */
    uint16_t rejected_mask;
    uint16_t tested_mask;
    uint32_t generation;
    /* All-zero trailing fields represent original v1.13f coarse state. */
    vts_fsm_level_t level;
    bool fine_valid;
    uint8_t fine_parent_index;
    uint8_t fine_current_index;
    uint16_t fine_rejected_mask;
    uint16_t fine_tested_mask;
    uint32_t fine_generation;
    uint32_t fine_coarse_generation;
} vts_persist_state_t;

bool vts_persist_state_valid(const vts_persist_state_t *s);
int32_t vts_persist_phase(const vts_persist_state_t *s);
/* Original exact 24-byte legacy codec retained for backward parity/testing. */
bool vts_persist_encode(const vts_phase_fsm_t *fsm, uint8_t out[VTS_PERSIST_SIZE]);
bool vts_persist_decode(const uint8_t raw[VTS_PERSIST_SIZE], vts_persist_state_t *out);
/* New codec used for ALL newly saved Permanent selections; read either. */
bool vts_persist_encode_v2(const vts_phase_fsm_t *fsm, uint8_t out[VTS_PERSIST_V2_SIZE]);
bool vts_persist_decode_any(const uint8_t *raw, size_t length, vts_persist_state_t *out);
#endif
