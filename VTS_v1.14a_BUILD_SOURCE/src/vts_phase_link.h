#ifndef VTS_PHASE_LINK_H
#define VTS_PHASE_LINK_H
#include "vts_phase_controller.h"
#include "vts_stage2_config.h"
#include "vts_impedance.h"
#include <stddef.h>
/* Single-owner, portable firmware integration engine. NO hardware functions.
 * All inputs must use monotonic milliseconds. The Zephyr main thread is the
 * sole owner; IRQs/feedback thread only post events into its message queue.
 * Tickets identify locally queued operations, NOT a CLIC wire transaction ID.
 * CLIC opcodes 29/30 have no wire nonce; stale on-air replies cannot be fully
 * distinguished. See LIMITATIONS in STAGE2_README.md. */
typedef enum { VTS_LINK_IDLE, VTS_LINK_WAIT_SET, VTS_LINK_WAIT_GET,
    VTS_LINK_BACKOFF, VTS_LINK_FEEDBACK, VTS_LINK_CONFLICT,
    VTS_LINK_FAULT, VTS_LINK_EXHAUSTED } vts_link_state_t;
typedef enum { VTS_GET_NONE, VTS_GET_PREFLIGHT, VTS_GET_VERIFY,
               VTS_GET_IDLE, VTS_GET_IMP_SCALE, VTS_GET_IMP_STATUS,
               VTS_GET_RESTORE_PREFLIGHT } vts_get_reason_t;
typedef struct { uint32_t ticket; uint8_t opcode; int32_t phase; } vts_link_command_t;
struct vts_phase_link;
typedef void (*vts_link_report_fn)(void *ctx, const struct vts_phase_link *l,
                                  const char *event, int32_t detail);
typedef struct vts_phase_link {
    vts_phase_controller_t ctrl;
    vts_link_state_t state;
    vts_get_reason_t get_reason;
    vts_fsm_action_t action; /* last/current decision, retained for commit diagnostics */
    vts_link_command_t command;
    bool output_ready, command_waiting, ack_seen, response_seen;
    uint32_t response_raw;
    uint64_t command_queued_ms, ack_ms, next_probe_ms, next_idle_poll_ms;
    uint64_t last_rx_ms, last_replay_ms, txn_started_ms;
    bool have_rx, startup, set_reply_seen, warning_printed;
    bool restore_pending;
    vts_persist_state_t restored;
    uint8_t get_attempts, replays;
    int32_t initial_phase, target_phase;
    int play_rc, restore_rc;
    bool feedback_ready;
    bool raw_down, stable_down, raw_press_blocked, ignore_boot_hold;
    uint64_t raw_changed_ms;
    vts_link_report_fn report;
    void *report_ctx;
    /* Read-only telemetry shares the same owner/FIFO. Core FSM is unchanged. */
    bool impedance_enabled;
    uint64_t next_impedance_poll_ms, next_scale_poll_ms;
    vts_impedance_t impedance;
} vts_phase_link_t;
void vts_link_init(vts_phase_link_t *, uint64_t now_ms, bool held_on_boot,
                   vts_link_report_fn, void *ctx);
bool vts_link_configure_impedance(vts_phase_link_t *, bool enabled);
/* Must be configured before vts_link_start(), before any command or RX. */
bool vts_link_set_permanent_restore(vts_phase_link_t *, const vts_persist_state_t *);
void vts_link_impedance_snapshot(const vts_phase_link_t *, uint64_t now_ms,
                                  vts_impedance_snapshot_t *out);
void vts_link_start(vts_phase_link_t *, uint64_t now_ms);
void vts_link_activity(vts_phase_link_t *, uint64_t now_ms);
void vts_link_raw_button(vts_phase_link_t *, bool pressed, uint64_t now_ms);
void vts_link_tick(vts_phase_link_t *, uint64_t now_ms);
bool vts_link_take_command(vts_phase_link_t *, vts_link_command_t *out);
void vts_link_queue_result(vts_phase_link_t *, uint32_t ticket, int rc, uint64_t now_ms);
void vts_link_ack(vts_phase_link_t *, uint32_t ticket, uint64_t now_ms);
void vts_link_response(vts_phase_link_t *, uint32_t ticket, uint8_t opcode,
                        uint32_t raw, uint64_t now_ms);
bool vts_link_take_feedback(vts_phase_link_t *, uint32_t *ticket, uint16_t *duration_ms);
void vts_link_feedback_done(vts_phase_link_t *, uint32_t ticket,
                            int play_rc, int restore_rc, uint64_t now_ms);
void vts_link_fail(vts_phase_link_t *, const char *reason, uint64_t now_ms);
bool vts_link_pending(const vts_phase_link_t *);
bool vts_link_invariants(const vts_phase_link_t *);
const char *vts_link_state_name(vts_link_state_t);
const char *vts_link_mode_name(vts_fsm_mode_t);
const char *vts_link_action_name(const vts_phase_link_t *);
#endif
