#include "vts_impedance.h"
#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "CLIC scale requires IEEE754 binary32 float");
static uint32_t swap32(uint32_t x)
{
    return ((x & 0x000000ffU) << 24) | ((x & 0x0000ff00U) << 8) |
           ((x & 0x00ff0000U) >> 8) | ((x & 0xff000000U) >> 24);
}
void vts_impedance_init(vts_impedance_t *v)
{ if (v) memset(v, 0, sizeof(*v)); }
void vts_impedance_invalidate(vts_impedance_t *v)
{
    if (!v) return;
    /* Keep raw evidence, but never advertise it as a current measurement. */
    v->scale_known = false; v->scale_valid = false; v->lead_known = false;
}
bool vts_impedance_reply(vts_impedance_t *v, uint8_t opcode, uint32_t raw, uint64_t now)
{
    if (!v) return false;
    if (opcode == VTS_IMP_GET_SCALE) {
        v->scale_bits = swap32(raw);
        v->scale_known = true; v->scale_ms = now;
        /* Accept only finite strictly-positive scaling. No invented clinical
         * threshold or hard-coded calibration coefficient. */
        v->scale_valid = (v->scale_bits & 0x80000000U) == 0U &&
                        (v->scale_bits & 0x7fffffffU) != 0U &&
                        (v->scale_bits & 0x7f800000U) != 0x7f800000U;
        v->scale = 0.0f;
        if (v->scale_valid) memcpy(&v->scale, &v->scale_bits, sizeof(v->scale));
        /* Pair a refreshed scale only with a subsequent status read. */
        v->lead_known = false;
        return true;
    }
    if (opcode == VTS_IMP_GET_STATUS) {
        v->flags = (uint8_t)(raw >> 24);
        v->amplitude = raw & 0x00ffffffU;
        v->lead_known = true; v->lead_ms = now;
        return true;
    }
    return false;
}
void vts_impedance_timeout(vts_impedance_t *v, uint8_t opcode)
{
    if (!v) return;
    if (opcode == VTS_IMP_GET_SCALE) {
        v->scale_known = false; v->scale_valid = false; v->lead_known = false;
    } else if (opcode == VTS_IMP_GET_STATUS) v->lead_known = false;
}
static int64_t age(uint64_t now, uint64_t then, bool known)
{
    if (!known || now < then || now - then > (uint64_t)INT64_MAX) return -1;
    return (int64_t)(now - then);
}
void vts_impedance_snapshot(const vts_impedance_t *v, uint64_t now, bool usable,
                             vts_impedance_snapshot_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->state = VTS_IMP_NOT_READY; s->kohm = -1.0;
    s->lead_age_ms = s->scale_age_ms = -1;
    if (!v) return;
    s->flags = v->flags; s->amplitude = v->amplitude; s->scale_bits = v->scale_bits;
    s->lead_age_ms = age(now, v->lead_ms, v->lead_known);
    s->scale_age_ms = age(now, v->scale_ms, v->scale_known);
    if (!usable) { s->state = VTS_IMP_NO_LINK; return; }
    if (!v->lead_known) return;
    if (s->lead_age_ms < 0 || s->lead_age_ms >= VTS_IMP_STATUS_TTL_MS) {
        s->state = VTS_IMP_STALE; return;
    }
    /* Match Monitor: flag bit 0 controls whether impedance is enabled.
     * Other flag bits are retained, but their semantics are not guessed. */
    if (!(v->flags & 1U)) { s->state = VTS_IMP_DISABLED; return; }
    if (!v->scale_known) return;
    if (!v->scale_valid) { s->state = VTS_IMP_BAD_SCALE; return; }
    if (s->scale_age_ms < 0 || s->scale_age_ms >= VTS_IMP_SCALE_TTL_MS) {
        s->state = VTS_IMP_STALE; return;
    }
    /* Python multiplies its float32-decoded scale in binary64. Do the same. */
    double value = (double)v->amplitude * (double)v->scale;
    if (!(value >= 0.0 && value <= (double)UINT32_MAX)) {
        /* Representation limit for integer telemetry, NOT a clinical limit. */
        s->state = VTS_IMP_OUT_OF_RANGE; return;
    }
    s->kohm = value;
    s->rounded_kohm = (uint32_t)(value + 0.5); /* nearest integer, halves up */
    s->millikohm = (uint64_t)(value * 1000.0 + 0.5);
    s->valid = true; s->state = VTS_IMP_OK;
}
bool vts_impedance_pack_get(uint8_t opcode, uint8_t out[5])
{
    if (!out || (opcode != VTS_IMP_GET_STATUS && opcode != VTS_IMP_GET_SCALE)) return false;
    out[0] = opcode; out[1] = out[2] = out[3] = 0;
    out[4] = opcode == VTS_IMP_GET_SCALE ? VTS_IMP_SCALE_SENSOR : 0U;
    return true;
}
const char *vts_impedance_state_name(vts_impedance_state_t s)
{
    switch (s) {
    case VTS_IMP_OK: return "OK"; case VTS_IMP_DISABLED: return "DISABLED";
    case VTS_IMP_NO_LINK: return "NO_LINK"; case VTS_IMP_STALE: return "STALE";
    case VTS_IMP_BAD_SCALE: return "BAD_SCALE";
    case VTS_IMP_OUT_OF_RANGE: return "OUT_OF_RANGE";
    default: return "NOT_READY";
    }
}
