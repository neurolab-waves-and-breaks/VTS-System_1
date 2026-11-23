#ifndef VTS_RECOVERY_POLICY_H
#define VTS_RECOVERY_POLICY_H
#include <stdint.h>
typedef enum {
    VTS_READBACK_MATCH_TARGET = 0,
    VTS_READBACK_OTHER_SUPPORTED_CHOICE,
    VTS_READBACK_EXTERNAL_PHASE,
} vts_readback_result_t;
vts_readback_result_t vts_classify_phase_readback(int32_t requested, int32_t actual);
#endif
