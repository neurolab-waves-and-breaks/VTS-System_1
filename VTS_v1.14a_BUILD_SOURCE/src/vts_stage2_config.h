#ifndef VTS_STAGE2_CONFIG_H
#define VTS_STAGE2_CONFIG_H
/* Explicit BENCH parameters, not clinical timing guarantees.
 * Dwell is fixed in the approved FSM: VTS_FSM_DWELL_MS = 240000.
 * Activity is inferred from valid CLIC packet reception. A missing packet
 * cannot be detected instantly: allow at most 1000 ms of inferred activity
 * after the last valid packet, then pause until reception resumes.
 * This does NOT establish that stimulation is enabled or actually delivered.
 */
#define VTS_LINK_ACTIVITY_TIMEOUT_MS 1000U
#define VTS_LINK_REPLY_GRACE_MS 3000U
#define VTS_LINK_GET_BATCH_LIMIT 3U
#define VTS_LINK_REPROBE_MS 5000U
#define VTS_LINK_SET_REPLAY_LIMIT 3U
#define VTS_LINK_REPLAY_COOLDOWN_MS 8000U
#define VTS_LINK_IDLE_POLL_MS 5000U
#define VTS_LINK_WARNING_MS 2000U
#define VTS_LINK_BUTTON_DEBOUNCE_MS 25U
/* Shorter automatic buzz has NOT been agreed: preserve the known 500 ms
 * feedback for ALL non-startup confirmed actions in this diagnostic build. */
#define VTS_LINK_FEEDBACK_MS 500U
#define VTS_LINK_SERVICE_MS 10U
#endif
