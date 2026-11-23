#ifndef VTS_PHASE_FSM_V2_H
#define VTS_PHASE_FSM_V2_H
/* Portable dual-resolution receiver FSM. NO Zephyr/ESB/haptic dependencies.
 * All phase/mask commits require a verified CLIC GET/SET/GET and completion
 * of the original 500 ms feedback path (performed by vts_phase_link.c).
 * The v1.13f coarse fields retain their names and their original meaning.
 */
#include <stdbool.h>
#include <stdint.h>

#define VTS_FSM_PHASE_COUNT 12U
#define VTS_FSM_ALL_PHASES_MASK UINT16_C(0x0FFF)
#define VTS_FSM_FINE_COUNT 7U
#define VTS_FSM_ALL_FINE_MASK UINT16_C(0x007F)
#define VTS_FSM_DWELL_MS UINT64_C(240000)

typedef enum {
    VTS_FSM_AWAIT_STARTUP = 0,
    VTS_FSM_CYCLING,
    VTS_FSM_PERMANENT,
    VTS_FSM_PENDING,
    VTS_FSM_FAULT
} vts_fsm_mode_t;

typedef enum { VTS_LEVEL_COARSE = 0, VTS_LEVEL_FINE = 1 } vts_fsm_level_t;

typedef enum {
    VTS_EVT_TIMER = 0,
    VTS_EVT_SHORT_PRESS,
    VTS_EVT_LONG_PRESS,
    VTS_EVT_DOUBLE_PRESS
} vts_fsm_event_t;

typedef enum {
    VTS_ACT_NONE = 0,
    VTS_ACT_AUTO_ADVANCE,
    VTS_ACT_REJECT_ADVANCE,
    VTS_ACT_ENTER_PERMANENT,
    VTS_ACT_EXIT_PERMANENT_ADVANCE,
    VTS_ACT_EXIT_PERMANENT_RESET,
    VTS_ACT_ENTER_FINE,
    VTS_ACT_EXIT_FINE
} vts_fsm_action_t;

typedef struct {
    vts_fsm_action_t action;
    int32_t target_phase;
    bool resets_all_phases_on_commit;
} vts_fsm_request_t;

typedef struct {
    vts_fsm_mode_t mode;
    uint8_t current_index;       /* ORIGINAL coarse index (0..11) */
    uint16_t rejected_mask;     /* ORIGINAL coarse history */
    uint16_t tested_mask;
    uint16_t permanent_mask;
    uint32_t generation;
    uint64_t phase_started_ms;
    vts_fsm_request_t pending;
    /* New fields must never alter the meaning of the original coarse ring. */
    vts_fsm_level_t level;
    bool fine_valid;
    uint8_t fine_parent_index;    /* 0..11, coarse anchor for cached Fine */
    uint8_t fine_current_index;   /* 0..6 */
    uint16_t fine_rejected_mask;
    uint16_t fine_tested_mask;
    uint16_t fine_permanent_mask;
    uint32_t fine_generation;
    uint32_t fine_coarse_generation;
    /* Immutable while pending: source stays committed until feedback_done. */
    uint8_t pending_target_index;
    uint8_t pending_parent_index;
    vts_fsm_level_t pending_target_level;
    bool pending_new_fine;
} vts_phase_fsm_t;

void vts_fsm_init(vts_phase_fsm_t *fsm);
bool vts_fsm_startup_confirmed(vts_phase_fsm_t *fsm, int32_t verified_phase, uint64_t now_ms);
int32_t vts_fsm_current_phase(const vts_phase_fsm_t *fsm);
int32_t vts_fsm_phase_at(uint8_t index);
int32_t vts_fsm_fine_phase(uint8_t parent_index, uint8_t member_index);
uint8_t vts_fsm_active_index(const vts_phase_fsm_t *fsm);
uint8_t vts_fsm_active_count(const vts_phase_fsm_t *fsm);
uint16_t vts_fsm_active_rejected(const vts_phase_fsm_t *fsm);
uint16_t vts_fsm_active_tested(const vts_phase_fsm_t *fsm);
uint16_t vts_fsm_active_permanent(const vts_phase_fsm_t *fsm);
uint64_t vts_fsm_elapsed_ms(const vts_phase_fsm_t *fsm, uint64_t now_ms);
bool vts_fsm_propose(vts_phase_fsm_t *fsm, vts_fsm_event_t event,
                     uint64_t now_ms, vts_fsm_request_t *request);
bool vts_fsm_commit_verified(vts_phase_fsm_t *fsm, int32_t verified_clic_phase, uint64_t now_ms);
void vts_fsm_mark_fault(vts_phase_fsm_t *fsm);
bool vts_fsm_invariants_hold(const vts_phase_fsm_t *fsm);
#endif
