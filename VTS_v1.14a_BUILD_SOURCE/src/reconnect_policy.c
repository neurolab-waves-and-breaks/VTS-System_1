#include "reconnect_policy.h"
#include "phase_selection.h"

bool vts_allow_phase_set_replay(int32_t initial_phase, int32_t requested_phase,
                                int32_t observed_phase, bool button_request,
                                unsigned int replays_done, unsigned int replay_limit)
{
    return button_request && observed_phase == initial_phase &&
           observed_phase != requested_phase &&
           phase_selection_contains(initial_phase) &&
           phase_selection_contains(requested_phase) &&
           replays_done < replay_limit;
}

/* After a failed replay, rate-limit the next SET and verify the original
 * phase again; after the total budget is spent, do not transmit more SETs.
 * Any value other than original or target is treated as a foreign override.
 */
vts_recovery_decision_t vts_phase_recovery_decide(
    int32_t initial_phase, int32_t requested_phase, int32_t observed_phase,
    bool button_request, unsigned int replays_done, unsigned int replay_limit,
    bool cooldown_complete)
{
    if (observed_phase == requested_phase) {
        return VTS_RECOVERY_TARGET_CONFIRMED;
    }
    if (!button_request || observed_phase != initial_phase ||
        !phase_selection_contains(initial_phase) ||
        !phase_selection_contains(requested_phase) ||
        initial_phase == requested_phase) {
        return VTS_RECOVERY_EXTERNAL_CONFLICT;
    }
    if (replays_done >= replay_limit) {
        return VTS_RECOVERY_REPLAY_EXHAUSTED;
    }
    if (replays_done > 0U && !cooldown_complete) {
        return VTS_RECOVERY_REPLAY_BACKOFF;
    }
    return VTS_RECOVERY_SAFE_REPLAY;
}
