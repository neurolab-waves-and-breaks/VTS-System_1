#include "recovery_policy.h"
#include "phase_selection.h"

vts_readback_result_t vts_classify_phase_readback(int32_t requested, int32_t actual)
{
    if (requested == actual) return VTS_READBACK_MATCH_TARGET;
    if (phase_selection_contains(actual)) return VTS_READBACK_OTHER_SUPPORTED_CHOICE;
    return VTS_READBACK_EXTERNAL_PHASE;
}
