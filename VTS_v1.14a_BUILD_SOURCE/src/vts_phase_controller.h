#ifndef VTS_PHASE_CONTROLLER_H
#define VTS_PHASE_CONTROLLER_H
/* Single-owner portable controller; timestamps from the existing main loop.
 * The 350-ms double interval starts on the first DEBOUNCED short release and
 * ends at the second DEBOUNCED down. No CLIC transaction before classification.
 */
#include "vts_phase_fsm.h"
#include "vts_phase_persist.h"
#define VTS_CTRL_LONG_PRESS_MS UINT64_C(1000)
#define VTS_CTRL_DOUBLE_PRESS_MS UINT64_C(350)

typedef struct {
    uint64_t transaction_id; /* local token only; ESB/CLIC framing unchanged */
    vts_fsm_request_t phase;
} vts_ctrl_request_t;

typedef struct {
    vts_phase_fsm_t fsm;
    uint64_t wall_ms;
    uint64_t active_ms;
    bool clic_active;
    bool external_conflict;
    int32_t observed_phase;
    bool button_down;
    bool button_eligible;
    uint64_t button_down_ms;
    uint64_t next_transaction_id;
    uint64_t pending_transaction_id;
    /* New recogniser state: no raw ISRs, no second command owner. */
    bool single_candidate;
    uint64_t first_short_release_ms;
    bool second_down;
} vts_phase_controller_t;

void vts_ctrl_init(vts_phase_controller_t *c, uint64_t wall_ms);
bool vts_ctrl_set_clic_active(vts_phase_controller_t *c, bool active, uint64_t wall_ms);
bool vts_ctrl_startup_verified(vts_phase_controller_t *c, int32_t phase, uint64_t wall_ms);
bool vts_ctrl_startup_restored(vts_phase_controller_t *c, int32_t phase,
                               const vts_persist_state_t *saved, uint64_t wall_ms);
/* The link calls vts_ctrl_tick BEFORE accepting a debounced late second DOWN. */
bool vts_ctrl_single_due(const vts_phase_controller_t *c, uint64_t wall_ms);
bool vts_ctrl_button_down(vts_phase_controller_t *c, uint64_t wall_ms);
bool vts_ctrl_button_up(vts_phase_controller_t *c, uint64_t wall_ms, vts_ctrl_request_t *out);
bool vts_ctrl_tick(vts_phase_controller_t *c, uint64_t wall_ms, vts_ctrl_request_t *out);
bool vts_ctrl_confirm_verified(vts_phase_controller_t *c, uint64_t transaction_id,
                               int32_t phase, uint64_t wall_ms);
bool vts_ctrl_mark_fault(vts_phase_controller_t *c, uint64_t transaction_id, uint64_t wall_ms);
bool vts_ctrl_resume_pending(vts_phase_controller_t *c, uint64_t transaction_id, uint64_t wall_ms);
bool vts_ctrl_observe_phase(vts_phase_controller_t *c, int32_t phase, uint64_t wall_ms);
uint64_t vts_ctrl_elapsed_ms(const vts_phase_controller_t *c, uint64_t wall_ms);
bool vts_ctrl_invariants_hold(const vts_phase_controller_t *c);
#endif
