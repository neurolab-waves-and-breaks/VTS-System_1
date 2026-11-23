#ifndef VTS_IMPEDANCE_H
#define VTS_IMPEDANCE_H
#include <stdbool.h>
#include <stdint.h>
/* Wire meanings are copied from the supplied esb_monitor_5_1 GUI input fix:
 * esb_serial.py:get_vertical_scaling / get_lead_detect_status,
 * main.py:handle_device_status / handle_device_connect.
 * These freshness intervals are receiver display policy, NOT CLIC specs. */
#define VTS_IMP_GET_STATUS 12U
#define VTS_IMP_GET_SCALE 18U
#define VTS_IMP_SCALE_SENSOR 6U
#define VTS_IMP_POLL_MS 5000U
#define VTS_IMP_SCALE_REFRESH_MS 60000U
#define VTS_IMP_SCALE_RETRY_MS 10000U
#define VTS_IMP_STATUS_TTL_MS 15000U
#define VTS_IMP_SCALE_TTL_MS 120000U

typedef enum {
    VTS_IMP_NOT_READY, VTS_IMP_OK, VTS_IMP_DISABLED, VTS_IMP_NO_LINK,
    VTS_IMP_STALE, VTS_IMP_BAD_SCALE, VTS_IMP_OUT_OF_RANGE
} vts_impedance_state_t;
typedef struct {
    bool scale_known, scale_valid, lead_known;
    uint32_t scale_bits, amplitude;
    uint8_t flags;
    float scale;
    uint64_t scale_ms, lead_ms;
} vts_impedance_t;
typedef struct {
    vts_impedance_state_t state;
    bool valid;
    uint8_t flags;
    uint32_t amplitude, scale_bits, rounded_kohm;
    uint64_t millikohm;
    int64_t lead_age_ms, scale_age_ms;
    double kohm;
} vts_impedance_snapshot_t;

void vts_impedance_init(vts_impedance_t *);
void vts_impedance_invalidate(vts_impedance_t *);
/* raw_be is the generic four-byte BE interpretation from clic_reply.c.
 * Opcode 18 carries LE binary32; it must be byte-swapped before decoding. */
bool vts_impedance_reply(vts_impedance_t *, uint8_t opcode, uint32_t raw_be, uint64_t now_ms);
void vts_impedance_timeout(vts_impedance_t *, uint8_t opcode);
void vts_impedance_snapshot(const vts_impedance_t *, uint64_t now_ms, bool link_usable,
                             vts_impedance_snapshot_t *out);
bool vts_impedance_pack_get(uint8_t opcode, uint8_t out[5]);
const char *vts_impedance_state_name(vts_impedance_state_t);
#endif
