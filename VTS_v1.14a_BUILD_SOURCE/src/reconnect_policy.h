#ifndef VTS_RECONNECT_POLICY_H
#define VTS_RECONNECT_POLICY_H
#include <stdbool.h>
#include <stdint.h>
/* Replay only if GET proves CLIC still has the phase committed before press.
 * Another actual value may have been written by the Python Monitor.
 */
bool vts_allow_phase_set_replay(int32_t initial_phase, int32_t requested_phase,
                                int32_t observed_phase, bool button_request,
                                unsigned int replays_done, unsigned int replay_limit);
/* Pure policy for interpreting one application GET-phase reply after a lost
 * SET response. No radio actions are performed by this function.
 */
typedef enum {
    VTS_RECOVERY_TARGET_CONFIRMED = 0,
    VTS_RECOVERY_SAFE_REPLAY,
    VTS_RECOVERY_REPLAY_BACKOFF,
    VTS_RECOVERY_REPLAY_EXHAUSTED,
    VTS_RECOVERY_EXTERNAL_CONFLICT,
} vts_recovery_decision_t;

vts_recovery_decision_t vts_phase_recovery_decide(
    int32_t initial_phase, int32_t requested_phase, int32_t observed_phase,
    bool button_request, unsigned int replays_done, unsigned int replay_limit,
    bool cooldown_complete);
#endif
