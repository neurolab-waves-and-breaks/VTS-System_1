#include "vts_phase_link.h"
#include "phase_wire.h"
#include <limits.h>
#include <string.h>

#define GET_PHASE 29U
#define SET_PHASE 30U
static bool is_impedance_opcode(uint8_t opcode)
{ return opcode == VTS_IMP_GET_SCALE || opcode == VTS_IMP_GET_STATUS; }
static void emit(vts_phase_link_t *l, const char *event, int32_t detail)
{ if (l->report) l->report(l->report_ctx, l, event, detail); }
static bool stopped(const vts_phase_link_t *l)
{ return l->state == VTS_LINK_CONFLICT || l->state == VTS_LINK_FAULT || l->state == VTS_LINK_EXHAUSTED; }
bool vts_link_pending(const vts_phase_link_t *l)
{ return l->startup || l->ctrl.pending_transaction_id != 0U || stopped(l); }
static bool advance(vts_phase_link_t *l, uint64_t now)
{
    if (now < l->ctrl.wall_ms) return false;
    if (l->ctrl.clic_active && l->have_rx &&
        now - l->last_rx_ms >= VTS_LINK_ACTIVITY_TIMEOUT_MS) {
        uint64_t until = l->last_rx_ms + VTS_LINK_ACTIVITY_TIMEOUT_MS;
        if (until < l->ctrl.wall_ms) until = l->ctrl.wall_ms;
        (void)vts_ctrl_set_clic_active(&l->ctrl, false, until);
        if (l->impedance_enabled) {
            /* A short gap invalidates the measurement, not calibration.
             * Re-reading calibration at every 1-ms inactive/active boundary
             * could starve status reads on CLIC's low-rate ping traffic. */
            l->impedance.lead_known = false;
            l->next_impedance_poll_ms = until;
        }
        emit(l, "CLIC_INACTIVE", (int32_t)VTS_LINK_ACTIVITY_TIMEOUT_MS);
    }
    return vts_ctrl_set_clic_active(&l->ctrl, l->ctrl.clic_active, now);
}
void vts_link_fail(vts_phase_link_t *l, const char *why, uint64_t now)
{
    (void)advance(l, now);
    if (l->ctrl.fsm.mode == VTS_FSM_PENDING)
        (void)vts_ctrl_mark_fault(&l->ctrl, l->ctrl.pending_transaction_id, l->ctrl.wall_ms);
    l->state = VTS_LINK_FAULT;
    l->output_ready = false;
    l->feedback_ready = false;
    (void)vts_ctrl_set_clic_active(&l->ctrl, false, l->ctrl.wall_ms);
    emit(l, why, 0);
}
static void conflict(vts_phase_link_t *l, int32_t phase)
{
    l->ctrl.observed_phase = phase;
    l->ctrl.external_conflict = true;
    l->state = VTS_LINK_CONFLICT;
    l->output_ready = false;
    emit(l, "EXTERNAL_CONFLICT", phase);
}
/* Only a confirmed Permanent selection may be manually adjusted at CLIC.
 * The 12-phase FSM index/history still owns the ORIGINAL selected phase.
 * During exit, a pre-existing idle GET can finish after the long press has
 * reserved the exit; permit that readback until VTS issues its own SET.
 * This is NOT a general relaxation of GET-after-SET verification. */
static bool permanent_exit_pending(const vts_phase_link_t *l)
{
    return l->ctrl.fsm.mode == VTS_FSM_PENDING &&
           l->ctrl.pending_transaction_id != 0U &&
           (l->action == VTS_ACT_EXIT_PERMANENT_ADVANCE ||
            l->action == VTS_ACT_EXIT_PERMANENT_RESET);
}
static bool manual_phase_readback_allowed(const vts_phase_link_t *l)
{
    return (l->ctrl.fsm.mode == VTS_FSM_PERMANENT &&
            l->ctrl.pending_transaction_id == 0U) ||
           permanent_exit_pending(l);
}
static void queue_command(vts_phase_link_t *l, uint8_t opcode)
{
    if (l->command_waiting || l->output_ready || l->command.ticket == UINT32_MAX) {
        vts_link_fail(l, "COMMAND_OWNERSHIP_FAULT", l->ctrl.wall_ms); return;
    }
    l->command.ticket++;
    l->command.opcode = opcode;
    l->command.phase = opcode == SET_PHASE ? l->target_phase : 0;
    l->command_waiting = true;
    l->ack_seen = false;
    l->response_seen = false;
    l->command_queued_ms = l->ctrl.wall_ms;
    l->state = opcode == SET_PHASE ? VTS_LINK_WAIT_SET : VTS_LINK_WAIT_GET;
    l->output_ready = true;
    emit(l, opcode == SET_PHASE ? "SET_QUEUED" :
            opcode == VTS_IMP_GET_SCALE ? "IMP_SCALE_QUEUED" :
            opcode == VTS_IMP_GET_STATUS ? "IMP_STATUS_QUEUED" : "GET_QUEUED", opcode);
}
static void get_start(vts_phase_link_t *l, vts_get_reason_t reason, bool fresh_batch)
{
    l->get_reason = reason;
    if (fresh_batch) l->get_attempts = 0;
    l->get_attempts++;
    queue_command(l, reason == VTS_GET_IMP_SCALE ? VTS_IMP_GET_SCALE :
                     reason == VTS_GET_IMP_STATUS ? VTS_IMP_GET_STATUS : GET_PHASE);
}
void vts_link_init(vts_phase_link_t *l, uint64_t now, bool held,
                   vts_link_report_fn report, void *ctx)
{
    memset(l, 0, sizeof(*l));
    vts_ctrl_init(&l->ctrl, now);
    l->state = VTS_LINK_IDLE;
    l->initial_phase = l->target_phase = vts_fsm_phase_at(0);
    l->raw_down = l->stable_down = l->ignore_boot_hold = held;
    l->raw_changed_ms = now;
    l->report = report; l->report_ctx = ctx;
}
bool vts_link_set_permanent_restore(vts_phase_link_t *l, const vts_persist_state_t *saved)
{
    if (!l || !vts_persist_state_valid(saved) || l->startup ||
        l->command.ticket != 0U || l->ctrl.fsm.mode != VTS_FSM_AWAIT_STARTUP)
        return false;
    l->restored=*saved;
    l->restore_pending=true;
    l->target_phase=vts_persist_phase(saved);
    return true;
}
bool vts_link_configure_impedance(vts_phase_link_t *l, bool enabled)
{
    if (!l || l->command.ticket != 0U || l->startup) return false;
    l->impedance_enabled = enabled;
    vts_impedance_init(&l->impedance);
    l->next_scale_poll_ms = l->ctrl.wall_ms + 1000U;
    l->next_impedance_poll_ms = l->ctrl.wall_ms + 1000U;
    return true;
}
void vts_link_impedance_snapshot(const vts_phase_link_t *l, uint64_t now,
                                  vts_impedance_snapshot_t *out)
{
    vts_impedance_snapshot(&l->impedance, now,
        l->impedance_enabled && l->ctrl.clic_active && !stopped(l), out);
}
void vts_link_start(vts_phase_link_t *l, uint64_t now)
{
    if (!advance(l, now) || l->ctrl.fsm.mode != VTS_FSM_AWAIT_STARTUP ||
        l->command.ticket != 0U || stopped(l)) return;
    l->startup = true;
    l->txn_started_ms = now;
    /* Legacy clean boot remains SET(-60) -> GET(-60) unchanged. A saved
     * Permanent state instead performs GET(current) -> SET(saved) -> GET(saved),
     * never SET(-60) first. Any failed step remains fail-closed. */
    if (l->restore_pending) get_start(l, VTS_GET_RESTORE_PREFLIGHT, true);
    else queue_command(l, SET_PHASE);
}
void vts_link_activity(vts_phase_link_t *l, uint64_t now)
{
    if (!advance(l, now)) return;
    const bool was_active = l->ctrl.clic_active;
    if (l->impedance_enabled && l->have_rx && now - l->last_rx_ms >= VTS_IMP_STATUS_TTL_MS) {
        /* Long reception gap: re-fetch both calibration and measurement. */
        vts_impedance_invalidate(&l->impedance);
        l->next_scale_poll_ms = now;
        l->next_impedance_poll_ms = now;
    }
    l->have_rx = true; l->last_rx_ms = now;
    if (!stopped(l)) (void)vts_ctrl_set_clic_active(&l->ctrl, true, now);
    if (!was_active && !stopped(l)) emit(l, "CLIC_ACTIVE", 0);
}
static void accepted_request(vts_phase_link_t *l, const vts_ctrl_request_t *r)
{
    l->initial_phase = vts_fsm_current_phase(&l->ctrl.fsm);
    l->target_phase = r->phase.target_phase;
    l->action = r->phase.action;
    l->replays = 0; l->last_replay_ms = 0; l->set_reply_seen = false;
    l->txn_started_ms = l->ctrl.wall_ms; l->warning_printed = false;
    emit(l, "PHASE_REQUEST", (int32_t)r->phase.action);
    /* An outstanding idle GET must finish before another payload is queued.
     * It never counts as the verification for this newly reserved request. */
    if (l->state == VTS_LINK_IDLE) get_start(l, VTS_GET_PREFLIGHT, true);
}
void vts_link_raw_button(vts_phase_link_t *l, bool down, uint64_t now)
{
    if (!advance(l, now) || down == l->raw_down) return;
    l->raw_down = down; l->raw_changed_ms = now;
    if (down) l->raw_press_blocked = vts_link_pending(l) || l->state == VTS_LINK_FEEDBACK;
}
static void debounce(vts_phase_link_t *l)
{
    if (l->stable_down == l->raw_down ||
        l->ctrl.wall_ms - l->raw_changed_ms < VTS_LINK_BUTTON_DEBOUNCE_MS) return;
    l->stable_down = l->raw_down;
    if (l->ignore_boot_hold) {
        if (!l->stable_down) { l->ignore_boot_hold = false; emit(l, "BUTTON_ARMED", 0); }
        return;
    }
    if (l->stable_down) {
        /* A late second down must first classify the first tap as SINGLE.
         * At the exact +350 ms boundary, the second down wins (DOUBLE).
         * The command owner remains the existing preflight/SET/GET path. */
        vts_ctrl_request_t r;
        if (vts_ctrl_single_due(&l->ctrl, l->ctrl.wall_ms) &&
            vts_ctrl_tick(&l->ctrl, l->ctrl.wall_ms, &r))
            accepted_request(l, &r);
        (void)vts_ctrl_button_down(&l->ctrl, l->ctrl.wall_ms);
        if (l->raw_press_blocked) l->ctrl.button_eligible = false;
        emit(l, "BUTTON_DOWN", l->ctrl.button_eligible ? 1 : 0);
    } else {
        vts_ctrl_request_t r;
        if (vts_ctrl_button_up(&l->ctrl, l->ctrl.wall_ms, &r)) accepted_request(l, &r);
        else emit(l, l->ctrl.single_candidate ? "BUTTON_CANDIDATE" : "BUTTON_IGNORED", 0);
    }
}
static void finish_verified(vts_phase_link_t *l)
{
    emit(l, "PHASE_VERIFIED", l->target_phase);
    if (l->startup) {
        const bool restored=l->restore_pending;
        const bool confirmed=restored ?
            vts_ctrl_startup_restored(&l->ctrl,l->target_phase,&l->restored,l->ctrl.wall_ms) :
            vts_ctrl_startup_verified(&l->ctrl,l->target_phase,l->ctrl.wall_ms);
        if (!confirmed) {
            vts_link_fail(l, "STARTUP_COMMIT_FAULT", l->ctrl.wall_ms); return;
        }
        l->restore_pending=false;
        l->startup = false; l->state = VTS_LINK_IDLE;
        l->next_idle_poll_ms = l->ctrl.wall_ms + VTS_LINK_IDLE_POLL_MS;
        emit(l, restored ? "RESTORE_CONFIRMED" : "STARTUP_CONFIRMED", l->target_phase);
    } else {
        l->state = VTS_LINK_FEEDBACK;
        l->feedback_ready = true;
        emit(l, "FEEDBACK_QUEUED", VTS_LINK_FEEDBACK_MS);
    }
}
static void replay_or_wait(vts_phase_link_t *l)
{
    if (l->replays >= VTS_LINK_SET_REPLAY_LIMIT) {
        if (l->ctrl.fsm.mode == VTS_FSM_PENDING)
            (void)vts_ctrl_mark_fault(&l->ctrl, l->ctrl.pending_transaction_id, l->ctrl.wall_ms);
        l->state = VTS_LINK_EXHAUSTED;
        (void)vts_ctrl_set_clic_active(&l->ctrl, false, l->ctrl.wall_ms);
        emit(l, "RECOVERY_EXHAUSTED", l->replays); return;
    }
    if (l->replays > 0U &&
        l->ctrl.wall_ms - l->last_replay_ms < VTS_LINK_REPLAY_COOLDOWN_MS) {
        l->state = VTS_LINK_BACKOFF;
        l->get_reason = VTS_GET_VERIFY;
        l->next_probe_ms = l->last_replay_ms + VTS_LINK_REPLAY_COOLDOWN_MS;
        emit(l, "REPLAY_BACKOFF", VTS_LINK_REPLAY_COOLDOWN_MS); return;
    }
    ++l->replays; l->last_replay_ms = l->ctrl.wall_ms;
    l->set_reply_seen = false;
    emit(l, "VERIFIED_SET_REPLAY", l->replays);
    queue_command(l, SET_PHASE);
}
static void resolve_response(vts_phase_link_t *l)
{
    if (!l->command_waiting || !l->ack_seen || !l->response_seen) return;
    const uint32_t raw = l->response_raw;
    const uint8_t opcode = l->command.opcode;
    l->command_waiting = false;
    l->output_ready = false;
    if (opcode == SET_PHASE) {
        if (raw != 0U) { vts_link_fail(l, "SET_REJECTED", l->ctrl.wall_ms); return; }
        l->set_reply_seen = true;
        emit(l, "SET_REPLY_ACCEPTED", l->target_phase);
        /* A SET status reply is not a phase readback. Always GET before buzz. */
        get_start(l, VTS_GET_VERIFY, true);
        return;
    }
    if (is_impedance_opcode(opcode)) {
        const bool right_owner =
            (opcode == VTS_IMP_GET_SCALE && l->get_reason == VTS_GET_IMP_SCALE) ||
            (opcode == VTS_IMP_GET_STATUS && l->get_reason == VTS_GET_IMP_STATUS);
        if (!right_owner || !l->impedance_enabled) {
            vts_link_fail(l, "IMPEDANCE_OWNER_FAULT", l->ctrl.wall_ms); return;
        }
        (void)vts_impedance_reply(&l->impedance, opcode, raw, l->ctrl.wall_ms);
        l->state = VTS_LINK_IDLE;
        if (opcode == VTS_IMP_GET_SCALE) {
            l->next_scale_poll_ms = l->ctrl.wall_ms +
                (l->impedance.scale_valid ? VTS_IMP_SCALE_REFRESH_MS : VTS_IMP_SCALE_RETRY_MS);
            l->next_impedance_poll_ms = l->ctrl.wall_ms; /* fresh status after scale */
            emit(l, "IMP_SCALE_REPLY", l->impedance.scale_valid ? 1 : 0);
        } else {
            l->next_impedance_poll_ms = l->ctrl.wall_ms + VTS_IMP_POLL_MS;
            emit(l, "IMP_STATUS_REPLY", (int32_t)l->impedance.flags);
        }
        /* A telemetry reply is NEVER phase verification. Reserved actions
         * must still perform the unchanged phase GET/SET/GET sequence. */
        if (l->ctrl.pending_transaction_id != 0U) get_start(l, VTS_GET_PREFLIGHT, true);
        return;
    }
    const int32_t actual = vts_decode_phase_parameter(raw);
    if (actual < -180 || actual > 180) {
        vts_link_fail(l, "INVALID_PHASE_READBACK", l->ctrl.wall_ms); return;
    }
    emit(l, "GET_REPLY", actual);
    if (l->get_reason == VTS_GET_RESTORE_PREFLIGHT) {
        if (!l->startup || !l->restore_pending) {
            vts_link_fail(l, "RESTORE_OWNER_FAULT", l->ctrl.wall_ms); return;
        }
        /* GET is diagnostic, not an external conflict: the saved Permanent
         * phase is authoritative on restart regardless of CLIC's old value. */
        l->initial_phase=actual;
        emit(l, "RESTORE_PREFLIGHT", actual);
        queue_command(l, SET_PHASE);
        return;
    }
    if (l->get_reason == VTS_GET_IDLE) {
        /* In confirmed Permanent, the practitioner may change CLIC directly
         * without changing the original VTS selection or phase history. */
        if (actual != vts_fsm_current_phase(&l->ctrl.fsm)) {
            if (!manual_phase_readback_allowed(l)) { conflict(l, actual); return; }
            emit(l, "PERMANENT_MANUAL_PHASE", actual);
        }
        l->ctrl.observed_phase = actual;
        l->state = VTS_LINK_IDLE;
        l->next_idle_poll_ms = l->ctrl.wall_ms + VTS_LINK_IDLE_POLL_MS;
        if (l->ctrl.pending_transaction_id != 0U) get_start(l, VTS_GET_PREFLIGHT, true);
        return;
    }
    if (l->get_reason == VTS_GET_PREFLIGHT) {
        if (actual != l->initial_phase) {
            /* Only a Permanent-exit action may reassert VTS ownership from
             * an externally adjusted phase. The next candidate is determined
             * by the ORIGINAL Permanent FSM index and rejected/tested masks. */
            if (!permanent_exit_pending(l)) { conflict(l, actual); return; }
            emit(l, "PERMANENT_EXIT_PREFLIGHT", actual);
        } else emit(l, "PREFLIGHT_MATCH", actual);
        queue_command(l, SET_PHASE);
        return;
    }
    if (l->get_reason != VTS_GET_VERIFY) {
        vts_link_fail(l, "GET_OWNER_FAULT", l->ctrl.wall_ms); return;
    }
    if (actual == l->target_phase &&
        (l->target_phase != l->initial_phase || l->set_reply_seen)) {
        finish_verified(l); return;
    }
    /* Same-angle actions (including Permanent/reset at -60) additionally need
     * the SET application reply: finding the old value alone proves nothing. */
    if (actual == l->initial_phase) { replay_or_wait(l); return; }
    conflict(l, actual);
}
void vts_link_tick(vts_phase_link_t *l, uint64_t now)
{
    if (!advance(l, now) || stopped(l)) return;
    debounce(l);
    if (stopped(l)) return;
    if (vts_link_pending(l) && !l->warning_printed &&
        now - l->txn_started_ms >= VTS_LINK_WARNING_MS && l->state != VTS_LINK_FEEDBACK) {
        l->warning_printed = true; emit(l, "STILL_WAITING", 0);
    }
    if (l->command_waiting && l->ack_seen && !l->response_seen &&
        now - l->ack_ms >= VTS_LINK_REPLY_GRACE_MS) {
        l->command_waiting = false;
        if (is_impedance_opcode(l->command.opcode)) {
            /* One telemetry attempt. Only retire it after the SDK ACK event;
             * never flush a live ACK FIFO or overlap the phase command owner. */
            l->output_ready = false;
            vts_impedance_timeout(&l->impedance, l->command.opcode);
            l->state = VTS_LINK_IDLE;
            l->next_impedance_poll_ms = now + VTS_IMP_POLL_MS;
            if (l->command.opcode == VTS_IMP_GET_SCALE)
                l->next_scale_poll_ms = now + VTS_IMP_SCALE_RETRY_MS;
            emit(l, "IMP_TIMEOUT", l->command.opcode);
        }
        else if (l->command.opcode == SET_PHASE) get_start(l, VTS_GET_VERIFY, true);
        else if (l->get_attempts < VTS_LINK_GET_BATCH_LIMIT) get_start(l, l->get_reason, false);
        else {
            l->state = VTS_LINK_BACKOFF;
            l->next_probe_ms = now + VTS_LINK_REPROBE_MS;
            emit(l, "GET_DEFERRED", VTS_LINK_REPROBE_MS);
        }
        return;
    }
    if (l->state == VTS_LINK_BACKOFF && now >= l->next_probe_ms && l->ctrl.clic_active) {
        get_start(l, l->get_reason, true); return;
    }
    if (l->state != VTS_LINK_IDLE || l->ctrl.fsm.mode == VTS_FSM_AWAIT_STARTUP) return;
    if (l->ctrl.pending_transaction_id != 0U) { get_start(l, VTS_GET_PREFLIGHT, true); return; }
    /* Raw high also defers expiry throughout the debounce interval. */
    if (!l->raw_down && !l->stable_down && !l->ignore_boot_hold) {
        vts_ctrl_request_t r;
        if (vts_ctrl_tick(&l->ctrl, now, &r)) { accepted_request(l, &r); return; }
    }
    if (now >= l->next_idle_poll_ms && l->ctrl.clic_active &&
        !l->raw_down && !l->stable_down) {
        get_start(l, VTS_GET_IDLE, true); return;
    }
    /* Lowest priority. Never during startup, a phase transaction, button hold,
     * recovery/backoff or feedback. An already-queued read is not preempted. */
    if (l->impedance_enabled && now >= l->next_impedance_poll_ms &&
        l->ctrl.clic_active && !l->raw_down && !l->stable_down && !l->ignore_boot_hold)
        get_start(l, now >= l->next_scale_poll_ms ? VTS_GET_IMP_SCALE : VTS_GET_IMP_STATUS, true);
}
bool vts_link_take_command(vts_phase_link_t *l, vts_link_command_t *out)
{
    if (!out || !l->output_ready || stopped(l)) return false;
    *out = l->command; l->output_ready = false; return true;
}
void vts_link_queue_result(vts_phase_link_t *l, uint32_t ticket, int rc, uint64_t now)
{
    if (!advance(l, now) || ticket != l->command.ticket || stopped(l)) return;
    if (rc != 0) vts_link_fail(l, "ESB_QUEUE_FAULT", now);
}
void vts_link_ack(vts_phase_link_t *l, uint32_t ticket, uint64_t now)
{
    if (!advance(l, now) || stopped(l) || !l->command_waiting || ticket != l->command.ticket) return;
    if (!l->ack_seen) {
        l->ack_seen = true; l->ack_ms = now;
        emit(l, "ACK_TX_SUCCESS", l->command.opcode);
    }
    resolve_response(l);
}
void vts_link_response(vts_phase_link_t *l, uint32_t ticket, uint8_t opcode,
                        uint32_t raw, uint64_t now)
{
    if (!advance(l, now) || stopped(l) || !l->command_waiting ||
        ticket != l->command.ticket || opcode != l->command.opcode || l->response_seen) {
        emit(l, "RESPONSE_IGNORED", opcode); return;
    }
    l->response_seen = true; l->response_raw = raw;
    resolve_response(l);
}
bool vts_link_take_feedback(vts_phase_link_t *l, uint32_t *ticket, uint16_t *duration)
{
    if (!ticket || !duration || !l->feedback_ready || l->state != VTS_LINK_FEEDBACK) return false;
    l->feedback_ready = false; *ticket = l->command.ticket; *duration = VTS_LINK_FEEDBACK_MS;
    return true;
}
void vts_link_feedback_done(vts_phase_link_t *l, uint32_t ticket, int play_rc,
                            int restore_rc, uint64_t now)
{
    if (!advance(l, now) || l->state != VTS_LINK_FEEDBACK || ticket != l->command.ticket) return;
    l->play_rc = play_rc; l->restore_rc = restore_rc;
    emit(l, "FEEDBACK_DONE", play_rc != 0 ? play_rc : restore_rc);
    /* CLIC already verified the phase. A vibration failure must not undo it
     * or cause another SET. Commit, then fail closed on actuator errors. */
    if (!vts_ctrl_confirm_verified(&l->ctrl, l->ctrl.pending_transaction_id,
                                    l->target_phase, now)) {
        vts_link_fail(l, "LOCAL_COMMIT_FAULT", now); return;
    }
    l->state = VTS_LINK_IDLE;
    l->next_idle_poll_ms = now + VTS_LINK_IDLE_POLL_MS;
    emit(l, "SELECTION_COMMITTED", l->target_phase);
    if (play_rc != 0 || restore_rc != 0) vts_link_fail(l, "HAPTIC_FAULT_PHASE_RETAINED", now);
}
bool vts_link_invariants(const vts_phase_link_t *l)
{
    if (!vts_ctrl_invariants_hold(&l->ctrl)) return false;
    if (l->output_ready && !l->command_waiting) return false;
    if (l->state == VTS_LINK_WAIT_SET && (!l->command_waiting || l->command.opcode != SET_PHASE)) return false;
    if (l->state == VTS_LINK_WAIT_GET && (!l->command_waiting ||
        (l->command.opcode != GET_PHASE && !is_impedance_opcode(l->command.opcode)))) return false;
    if (l->command_waiting && is_impedance_opcode(l->command.opcode) && !l->impedance_enabled) return false;
    if (l->state == VTS_LINK_FEEDBACK && (l->ctrl.pending_transaction_id == 0U || l->command_waiting)) return false;
    if (l->replays > VTS_LINK_SET_REPLAY_LIMIT) return false;
    return true;
}
const char *vts_link_state_name(vts_link_state_t s)
{
    switch (s) {
    case VTS_LINK_IDLE: return "IDLE"; case VTS_LINK_WAIT_SET: return "WAIT_SET";
    case VTS_LINK_WAIT_GET: return "WAIT_GET"; case VTS_LINK_BACKOFF: return "BACKOFF";
    case VTS_LINK_FEEDBACK: return "FEEDBACK"; case VTS_LINK_CONFLICT: return "CONFLICT";
    case VTS_LINK_FAULT: return "FAULT"; case VTS_LINK_EXHAUSTED: return "RECOVERY_EXHAUSTED";
    default: return "INVALID";
    }
}
const char *vts_link_mode_name(vts_fsm_mode_t s)
{
    switch (s) {
    case VTS_FSM_AWAIT_STARTUP: return "AWAIT_STARTUP"; case VTS_FSM_CYCLING: return "CYCLING";
    case VTS_FSM_PERMANENT: return "PERMANENT"; case VTS_FSM_PENDING: return "PENDING";
    case VTS_FSM_FAULT: return "FAULT"; default: return "INVALID";
    }
}
const char *vts_link_action_name(const vts_phase_link_t *l)
{
    if (l->startup) return "STARTUP";
    switch (l->action) {
    case VTS_ACT_NONE: return "NONE"; case VTS_ACT_AUTO_ADVANCE: return "AUTO_ADVANCE";
    case VTS_ACT_REJECT_ADVANCE: return "REJECT_ADVANCE";
    case VTS_ACT_ENTER_PERMANENT: return "ENTER_PERMANENT";
    case VTS_ACT_EXIT_PERMANENT_ADVANCE: return "EXIT_PERMANENT_ADVANCE";
    case VTS_ACT_EXIT_PERMANENT_RESET: return "EXIT_PERMANENT_RESET";
    case VTS_ACT_ENTER_FINE: return "ENTER_FINE";
    case VTS_ACT_EXIT_FINE: return "EXIT_FINE";
    default: return "INVALID";
    }
}
