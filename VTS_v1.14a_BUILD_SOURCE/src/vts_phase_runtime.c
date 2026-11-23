#include "vts_phase_runtime.h"
#include "vts_phase_link.h"
#include "clic.h"
#include "clic_reply.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <esb.h>
#include <string.h>

LOG_MODULE_REGISTER(vts_fsm, LOG_LEVEL_INF);
/* The phase/impedance engine remains the sole command owner. BLE is a
 * read-only observer and can only pause RX while the owner is settled. */
#if defined(CONFIG_BT) && !defined(CONFIG_VTS_BLE_TELEMETRY)
#error "Bluetooth requires the explicit v1.12a ble_telemetry.conf profile"
#endif
#if defined(CONFIG_VTS_BLE_TELEMETRY)
#include "ble_telemetry.h"
#include "vts_telemetry_snapshot.h"
#endif

enum input_type { INPUT_ACTIVITY, INPUT_ACK, INPUT_REPLY, INPUT_BUTTON, INPUT_FEEDBACK };
struct input_event {
    uint64_t at_ms;
    uint32_t ticket;
    uint32_t raw;
    int play_rc, restore_rc;
    uint8_t type, opcode, down;
};
K_MSGQ_DEFINE(phase_input_queue, sizeof(struct input_event), 64, 8);
static atomic_t overflow;
static atomic_t active_ticket;
static atomic_t raw_button_level;
static vts_phase_link_t link;
static bool enabled, fault_fifo_handled, persist_commit_pending;
static void impedance_status(void);
#if defined(CONFIG_VTS_BLE_TELEMETRY)
/* Non-mutating hardware readiness query, implemented beside existing haptics. */
extern bool vts_hw_telemetry_idle(void);
static bool ble_recheck(void)
{
    return !atomic_get(&overflow) && !atomic_get(&raw_button_level) &&
        k_msgq_num_used_get(&phase_input_queue)==0U && vts_hw_telemetry_idle() &&
        vts_telemetry_window_allowed(&link,(uint64_t)k_uptime_get(),CONFIG_VTS_BLE_WINDOW_MS);
}
#endif

static void report(void *ctx, const vts_phase_link_t *l, const char *event, int32_t detail)
{
    ARG_UNUSED(ctx);
    LOG_INF("VTS_FSM %s t_ms=%llu txn=%llu ticket=%u action=%s current_phase=%d target_phase=%d detail=%d mode=%s link=%s replay=%u/%u get=%u/%u",
            event, (unsigned long long)l->ctrl.wall_ms,
            (unsigned long long)l->ctrl.pending_transaction_id,
            l->command.ticket, vts_link_action_name(l),
            (int)vts_fsm_current_phase(&l->ctrl.fsm), (int)l->target_phase, (int)detail,
            vts_link_mode_name(l->ctrl.fsm.mode), vts_link_state_name(l->state),
            l->replays, VTS_LINK_SET_REPLAY_LIMIT, l->get_attempts, VTS_LINK_GET_BATCH_LIMIT);
    if (strcmp(event, "SELECTION_COMMITTED") == 0) {
        /* Only long-press Permanent entry/exit affects NVS. Automatic cycling,
         * short presses, impedance polls, and radio snapshots NEVER write. */
        if (l->action == VTS_ACT_ENTER_PERMANENT ||
            l->action == VTS_ACT_EXIT_PERMANENT_ADVANCE ||
            l->action == VTS_ACT_EXIT_PERMANENT_RESET)
            persist_commit_pending=true;
    }
#if defined(CONFIG_VTS_BLE_TELEMETRY)
    /* The phase was GET-verified, feedback has completed, and the controller
     * has committed. The main owner will persist NVS and recheck all radio
     * guards before any priority advertising begins. */
    if (strcmp(event, "SELECTION_COMMITTED") == 0)
        vts_ble_telemetry_request_priority();
    /* v1.13f: two prompt, CRC-valid broadcasts only AFTER startup / saved
     * Permanent GET/SET/GET verification. Never advertise an unverified
     * reset of history as if it were authoritative. */
    if (strcmp(event, "STARTUP_CONFIRMED") == 0 ||
        strcmp(event, "RESTORE_CONFIRMED") == 0)
        vts_ble_telemetry_request_startup_sync();
#endif
    if (strcmp(event, "SELECTION_COMMITTED") == 0 ||
        strcmp(event, "STARTUP_CONFIRMED") == 0 ||
        strcmp(event, "RESTORE_CONFIRMED") == 0)
        vts_runtime_status("COMMIT_STATE");
    if (strcmp(event, "IMP_SCALE_REPLY") == 0 || strcmp(event, "IMP_STATUS_REPLY") == 0 ||
        strcmp(event, "IMP_TIMEOUT") == 0) impedance_status();
}
static void post(struct input_event *ev)
{
    ev->at_ms = (uint64_t)k_uptime_get();
    if (k_msgq_put(&phase_input_queue, ev, K_NO_WAIT) != 0) atomic_set(&overflow, 1);
}
void vts_runtime_rx_activity(void)
{ struct input_event e = {.type=INPUT_ACTIVITY}; post(&e); }
void vts_runtime_ack(void)
{
    struct input_event e = {.type=INPUT_ACK, .ticket=(uint32_t)atomic_get(&active_ticket)};
    post(&e);
}
void vts_runtime_response(const uint8_t *body, uint32_t length)
{
    uint8_t opcode; uint32_t raw;
    if (!vts_clic_reply_decode(body, length, &opcode, &raw)) {
        LOG_WRN("VTS_FSM MALFORMED_RESPONSE length=%u", length); return;
    }
    struct input_event e = {.type=INPUT_REPLY, .opcode=opcode, .raw=raw,
                            .ticket=(uint32_t)atomic_get(&active_ticket)};
    post(&e);
}
void vts_runtime_button_edge(bool pressed)
{
    atomic_set(&raw_button_level, pressed ? 1 : 0);
    struct input_event e = {.type=INPUT_BUTTON, .down=pressed ? 1 : 0}; post(&e);
}
void vts_runtime_feedback_complete(uint32_t ticket, int play_rc, int restore_rc)
{
    struct input_event e = {.type=INPUT_FEEDBACK, .ticket=ticket,
                            .play_rc=play_rc, .restore_rc=restore_rc}; post(&e);
}
int vts_runtime_init(bool held)
{
    /* Called after NVS mount, but before enabling GPIO interrupts or ESB RX. */
    k_msgq_purge(&phase_input_queue);
    atomic_set(&overflow, 0); atomic_set(&active_ticket, 0);
    atomic_set(&raw_button_level, held ? 1 : 0);
    vts_link_init(&link, (uint64_t)k_uptime_get(), held, report, NULL);
#if defined(CONFIG_VTS_IMPEDANCE) && CONFIG_VTS_IMPEDANCE
    (void)vts_link_configure_impedance(&link, true);
#endif
    enabled = false; fault_fifo_handled = false; persist_commit_pending=false;
    vts_persist_state_t saved;
    int loaded=vts_hw_permanent_load(&saved);
    if (loaded < 0) {
        LOG_ERR("VTS_PERSIST BOOT_BLOCKED invalid_or_unreadable_record rc=%d",loaded);
        return loaded; /* Never silently reset a corrupt saved Permanent state. */
    }
    if (loaded == 1) {
        if (!vts_link_set_permanent_restore(&link,&saved)) {
            LOG_ERR("VTS_PERSIST BOOT_BLOCKED invalid_saved_state");
            return -EINVAL;
        }
        LOG_INF("VTS_PERSIST LOADED phase=%d rejected=0x%03X tested=0x%03X generation=%u awaiting_CLIC_GET_SET_GET=1",
                (int)vts_persist_phase(&saved),saved.rejected_mask,
                saved.tested_mask,(unsigned int)saved.generation);
    } else LOG_INF("VTS_PERSIST EMPTY default_startup=-60");
    enabled = true;
    LOG_INF("VTS_FSM CONFIG dwell_ms=%u inactivity_timeout_ms=%u idle_get_ms=%u feedback_ms=%u ble=%u impedance=%u timer_policy=COMMUNICATION_LEGACY",
            (unsigned int)VTS_FSM_DWELL_MS, VTS_LINK_ACTIVITY_TIMEOUT_MS,
            VTS_LINK_IDLE_POLL_MS, VTS_LINK_FEEDBACK_MS, IS_ENABLED(CONFIG_BT) ? 1U : 0U,
            link.impedance_enabled ? 1U : 0U);
    return 0;
}
static void dispatch_outputs(void)
{
    if (persist_commit_pending) {
        persist_commit_pending=false;
        /* A confirmed Permanent transition is the ONLY flash-write event.
         * If NVS fails, stop the command owner: do not falsely promise that
         * power cycling will restore the selected phase. */
        int rc=link.ctrl.fsm.mode == VTS_FSM_PERMANENT ?
            vts_hw_permanent_save(&link.ctrl.fsm) : vts_hw_permanent_clear();
        if (rc != 0) {
            LOG_ERR("VTS_PERSIST COMMIT_FAILED rc=%d",rc);
            vts_link_fail(&link,"PERSIST_NVS_FAULT",(uint64_t)k_uptime_get());
        } else {
            LOG_INF("VTS_PERSIST COMMITTED mode=%s phase=%d rejected=0x%03X tested=0x%03X",
                    vts_link_mode_name(link.ctrl.fsm.mode),
                    (int)vts_fsm_current_phase(&link.ctrl.fsm),
                    vts_fsm_active_rejected(&link.ctrl.fsm),
                    vts_fsm_active_tested(&link.ctrl.fsm));
        }
    }
    /* An ISR can fill the input queue during the owner loop. Do not submit
     * another command/feedback after a detected lost input. */
    if (atomic_get(&overflow) && link.state != VTS_LINK_FAULT)
        vts_link_fail(&link, "EVENT_QUEUE_OVERFLOW", (uint64_t)k_uptime_get());
    vts_link_command_t command;
    if (vts_link_take_command(&link, &command)) {
        /* Publish cookie before ESB submission: interrupt events are queued
         * and cannot mutate the engine until this owner processes them. */
        atomic_set(&active_ticket, (atomic_val_t)command.ticket);
        int rc;
        if (command.opcode == 30U) rc = clic_send_phase(command.phase);
        else if (command.opcode == 29U) rc = clic_get_phase();
        else {
            uint8_t body[CLIC_CMD_BODY_BYTES];
            rc = vts_impedance_pack_get(command.opcode, body) ? clic_send_command(body) : -EINVAL;
        }
        vts_link_queue_result(&link, command.ticket, rc, (uint64_t)k_uptime_get());
    }
    uint32_t ticket; uint16_t duration;
    if (vts_link_take_feedback(&link, &ticket, &duration)) {
        int rc = vts_hw_submit_feedback(ticket, duration);
        if (rc != 0) vts_link_feedback_done(&link, ticket, rc, 0, (uint64_t)k_uptime_get());
    }
    if (!fault_fifo_handled && (link.state == VTS_LINK_FAULT ||
        link.state == VTS_LINK_CONFLICT || link.state == VTS_LINK_EXHAUSTED)) {
        /* Only this owner ever submits CLIC commands. On terminal error drop
         * any unsent command so reconnection cannot execute an abandoned SET.
         * This does NOT undo commands that CLIC has already executed. */
        /* In the reference SDK esb_flush_tx requires IDLE, not PRX RX.
         * Stop first; on a terminal fault leave this test receiver stopped.
         * Never resume an ambiguous queued SET after a terminal error. */
        int stop_rc = esb_stop_rx();
        int flush_rc = esb_flush_tx();
        if ((stop_rc != 0 && stop_rc != -EALREADY) || flush_rc != 0) {
            esb_disable();
        }
        LOG_ERR("VTS_FSM PAUSED state=%s stop_rx_rc=%d ack_fifo_flush_rc=%d radio_stopped=1 reset_required=1",
                vts_link_state_name(link.state), stop_rc, flush_rc);
        atomic_set(&active_ticket, 0);
        fault_fifo_handled = true;
    }
}
void vts_runtime_start(void)
{
    /* An invalid/unreadable saved Permanent record must block startup SET. */
    if (!enabled) return;
    vts_link_start(&link, (uint64_t)k_uptime_get()); dispatch_outputs();
}
void vts_runtime_poll(void)
{
    if (!enabled) return;
#if defined(CONFIG_VTS_BLE_TELEMETRY)
    /* Do not dispatch a CLIC command until ESB reception has resumed. GPIO
     * timestamps are retained in the existing queue; a press ends BLE early.
     * No workqueue callback can independently restart ESB after a fault. */
    vts_ble_telemetry_service(atomic_get(&overflow) || atomic_get(&raw_button_level) ||
        k_msgq_num_used_get(&phase_input_queue)!=0U, (uint64_t)k_uptime_get());
    if (vts_ble_telemetry_failed()) {
        if (link.state != VTS_LINK_FAULT)
            vts_link_fail(&link,"BLE_RADIO_HANDOFF_FAULT",(uint64_t)k_uptime_get());
        k_msgq_purge(&phase_input_queue);dispatch_outputs();return;
    }
    if (vts_ble_telemetry_busy()) return;
#endif
    if (atomic_get(&overflow)) {
        if (link.state != VTS_LINK_FAULT)
            vts_link_fail(&link, "EVENT_QUEUE_OVERFLOW", (uint64_t)k_uptime_get());
        k_msgq_purge(&phase_input_queue); dispatch_outputs(); return;
    }
    struct input_event e;
    unsigned int n = 0;
    while (n++ < 128U && k_msgq_get(&phase_input_queue, &e, K_NO_WAIT) == 0) {
        /* A callback can enqueue just before the preceding owner tick. Clamp
         * such sub-tick lateness; never run the controller clock backwards. */
        uint64_t now = e.at_ms < link.ctrl.wall_ms ? link.ctrl.wall_ms : e.at_ms;
        switch (e.type) {
        case INPUT_ACTIVITY: vts_link_activity(&link, now); break;
        case INPUT_ACK: vts_link_ack(&link, e.ticket, now); break;
        case INPUT_REPLY:
            vts_link_activity(&link, now);
            vts_link_response(&link, e.ticket, e.opcode, e.raw, now); break;
        case INPUT_BUTTON: vts_link_raw_button(&link, e.down != 0U, now); break;
        case INPUT_FEEDBACK:
            vts_link_feedback_done(&link, e.ticket, e.play_rc, e.restore_rc, now); break;
        default: vts_link_fail(&link, "BAD_INPUT_EVENT", now); break;
        }
        dispatch_outputs();
    }
    uint64_t now = (uint64_t)k_uptime_get();
    /* If a GPIO edge raced the empty-queue check, honour the raw high before
     * considering timer expiry. The queued edge will be harmlessly redundant. */
    vts_link_raw_button(&link, atomic_get(&raw_button_level) != 0, now);
    vts_link_tick(&link, now);
    if (!vts_link_invariants(&link)) vts_link_fail(&link, "INVARIANT_FAILURE", now);
    dispatch_outputs();
#if defined(CONFIG_VTS_BLE_TELEMETRY)
    /* No mutation of link, FSM, timer or impedance by the snapshot builder. */
    if (vts_ble_telemetry_due((uint64_t)k_uptime_get()) && ble_recheck()) {
        vts_telemetry_packet_t snapshot;
        now=(uint64_t)k_uptime_get();
        if (vts_telemetry_snapshot(&link,now,0U,&snapshot))
            vts_ble_telemetry_poll(&snapshot,true,now,ble_recheck);
    }
#endif
}
void vts_runtime_status(const char *label)
{
    if (!enabled) return;
    const vts_phase_fsm_t *f = &link.ctrl.fsm;
    unsigned int available = 0U;
    for (unsigned int i = 0; i < vts_fsm_active_count(f); ++i)
        if ((vts_fsm_active_rejected(f) & (1U << i)) == 0U) ++available;
    LOG_INF("VTS_FSM %s t_ms=%llu mode=%s level=%s parent=%d link=%s current_phase=%d index=%u target_phase=%d pending=%u active=%u elapsed_ms=%llu available=%u tested=0x%03X rejected=0x%03X permanent=0x%03X generation=%u queue_overflow=%u",
            label, (unsigned long long)link.ctrl.wall_ms,
            vts_link_mode_name(f->mode), f->level == VTS_LEVEL_FINE ? "FINE" : "COARSE",
            f->level == VTS_LEVEL_FINE ? (int)vts_fsm_phase_at(f->fine_parent_index) : -999,
            vts_link_state_name(link.state),
            (int)vts_fsm_current_phase(f), vts_fsm_active_index(f) + 1U, (int)link.target_phase,
            vts_link_pending(&link), link.ctrl.clic_active,
            (unsigned long long)vts_ctrl_elapsed_ms(&link.ctrl, link.ctrl.wall_ms),
            available, vts_fsm_active_tested(f), vts_fsm_active_rejected(f),
            vts_fsm_active_permanent(f),
            f->level == VTS_LEVEL_FINE ? f->fine_generation : f->generation,
            (unsigned int)atomic_get(&overflow));
    impedance_status();
}

/* Owner-thread-only logging. No extra worker, radio driver or IRQ owner. */
static void impedance_status(void)
{
    if (!enabled || !link.impedance_enabled) return;
    vts_impedance_snapshot_t s;
    vts_link_impedance_snapshot(&link, link.ctrl.wall_ms, &s);
    if (s.valid) {
        LOG_INF("VTS_IMP STATUS t_ms=%llu state=OK valid=1 kohm=%llu.%03u display_kohm=%u flags=0x%02X amplitude=%u scale_bits=0x%08X lead_age_ms=%lld scale_age_ms=%lld",
            (unsigned long long)link.ctrl.wall_ms,
            (unsigned long long)(s.millikohm / 1000U), (unsigned int)(s.millikohm % 1000U),
            (unsigned int)s.rounded_kohm, (unsigned int)s.flags, (unsigned int)s.amplitude,
            (unsigned int)s.scale_bits, (long long)s.lead_age_ms, (long long)s.scale_age_ms);
    } else {
        LOG_INF("VTS_IMP STATUS t_ms=%llu state=%s valid=0 kohm=NA display_kohm=NA flags=0x%02X amplitude=%u scale_bits=0x%08X lead_age_ms=%lld scale_age_ms=%lld",
            (unsigned long long)link.ctrl.wall_ms, vts_impedance_state_name(s.state),
            (unsigned int)s.flags, (unsigned int)s.amplitude, (unsigned int)s.scale_bits,
            (long long)s.lead_age_ms, (long long)s.scale_age_ms);
    }
}
