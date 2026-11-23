#include "vts_phase_controller.h"
#include <stddef.h>

static bool time_is_running(const vts_phase_controller_t *c)
{
    return c->clic_active && !c->external_conflict &&
        (c->fsm.mode==VTS_FSM_CYCLING || c->fsm.mode==VTS_FSM_PERMANENT ||
         c->fsm.mode==VTS_FSM_PENDING);
}
static bool advance_time(vts_phase_controller_t *c, uint64_t now)
{
    if (!c || now<c->wall_ms) return false;
    if (time_is_running(c)) c->active_ms+=now-c->wall_ms;
    c->wall_ms=now;
    return true;
}
void vts_ctrl_init(vts_phase_controller_t *c, uint64_t now)
{
    if (!c) return;
    *c=(vts_phase_controller_t){0};
    vts_fsm_init(&c->fsm);
    c->wall_ms=now;c->observed_phase=INT32_MIN;c->next_transaction_id=1U;
}
bool vts_ctrl_set_clic_active(vts_phase_controller_t *c, bool active, uint64_t now)
{ if (!advance_time(c,now)) return false; c->clic_active=active;return true; }
bool vts_ctrl_startup_verified(vts_phase_controller_t *c, int32_t phase, uint64_t now)
{
    if (!advance_time(c,now) || c->external_conflict) return false;
    if (!vts_fsm_startup_confirmed(&c->fsm,phase,c->active_ms)) return false;
    c->observed_phase=phase;return true;
}
bool vts_ctrl_startup_restored(vts_phase_controller_t *c, int32_t phase,
                               const vts_persist_state_t *saved, uint64_t now)
{
    if (!c || !saved || !vts_persist_state_valid(saved) ||
        c->fsm.mode!=VTS_FSM_AWAIT_STARTUP || c->external_conflict ||
        phase!=vts_persist_phase(saved)) return false;
    vts_phase_controller_t next=*c;
    if (!advance_time(&next,now)) return false;
    vts_phase_fsm_t *s=&next.fsm;
    s->mode=VTS_FSM_PERMANENT;
    s->current_index=saved->current_index;
    s->rejected_mask=saved->rejected_mask;
    s->tested_mask=saved->tested_mask;
    s->generation=saved->generation;
    s->level=saved->level;
    if (saved->fine_valid) {
        s->fine_valid=true;
        s->fine_parent_index=saved->fine_parent_index;
        s->fine_current_index=saved->fine_current_index;
        s->fine_rejected_mask=saved->fine_rejected_mask;
        s->fine_tested_mask=saved->fine_tested_mask;
        s->fine_generation=saved->fine_generation;
        s->fine_coarse_generation=saved->fine_coarse_generation;
    }
    s->permanent_mask=s->level==VTS_LEVEL_COARSE ? (uint16_t)(1U<<s->current_index):0U;
    s->fine_permanent_mask=s->level==VTS_LEVEL_FINE ?
        (uint16_t)(1U<<s->fine_current_index):0U;
    s->phase_started_ms=next.active_ms;
    next.observed_phase=phase;
    if (!vts_ctrl_invariants_hold(&next)) return false;
    *c=next;return true;
}
static bool propose(vts_phase_controller_t *c, vts_fsm_event_t event, vts_ctrl_request_t *out)
{
    if (!out || c->external_conflict || c->pending_transaction_id!=0U ||
        c->next_transaction_id==UINT64_MAX) return false;
    vts_fsm_request_t r;
    if (!vts_fsm_propose(&c->fsm,event,c->active_ms,&r)) return false;
    c->pending_transaction_id=c->next_transaction_id++;
    *out=(vts_ctrl_request_t){c->pending_transaction_id,r};
    return true;
}
bool vts_ctrl_single_due(const vts_phase_controller_t *c, uint64_t now)
{
    return c && c->single_candidate && now>=c->first_short_release_ms &&
           now-c->first_short_release_ms>VTS_CTRL_DOUBLE_PRESS_MS;
}
bool vts_ctrl_button_down(vts_phase_controller_t *c, uint64_t now)
{
    if (!advance_time(c,now) || c->button_down) return false;
    c->button_down=true;c->button_down_ms=now;
    c->button_eligible=!c->external_conflict && c->pending_transaction_id==0U &&
        (c->fsm.mode==VTS_FSM_CYCLING || c->fsm.mode==VTS_FSM_PERMANENT);
    c->second_down=false;
    if (c->single_candidate && c->button_eligible &&
        now>=c->first_short_release_ms &&
        now-c->first_short_release_ms<=VTS_CTRL_DOUBLE_PRESS_MS) {
        c->second_down=true;
        c->single_candidate=false;
    } else if (c->single_candidate) {
        /* The caller must have flushed an overdue first candidate BEFORE
         * accepting this second down; never silently forget a first tap. */
        c->button_eligible=false;
    }
    return c->button_eligible;
}
bool vts_ctrl_button_up(vts_phase_controller_t *c, uint64_t now, vts_ctrl_request_t *out)
{
    if (!out || !advance_time(c,now) || !c->button_down) return false;
    bool eligible=c->button_eligible, second=c->second_down;
    uint64_t held=now-c->button_down_ms;
    c->button_down=false;c->button_eligible=false;c->second_down=false;
    if (!eligible) return false;
    if (held>=VTS_CTRL_LONG_PRESS_MS) {
        c->single_candidate=false;
        return propose(c,VTS_EVT_LONG_PRESS,out);
    }
    if (second) return propose(c,VTS_EVT_DOUBLE_PRESS,out);
    if (c->fsm.mode==VTS_FSM_PERMANENT) return false; /* Short always ignored. */
    c->single_candidate=true;c->first_short_release_ms=now;
    return false;
}
bool vts_ctrl_tick(vts_phase_controller_t *c, uint64_t now, vts_ctrl_request_t *out)
{
    if (!out || !advance_time(c,now) || c->button_down || c->external_conflict ||
        c->pending_transaction_id!=0U) return false;
    if (c->single_candidate) {
        if (!vts_ctrl_single_due(c,now)) return false;
        c->single_candidate=false;
        return propose(c,VTS_EVT_SHORT_PRESS,out);
    }
    if (!c->clic_active) return false;
    return propose(c,VTS_EVT_TIMER,out);
}
bool vts_ctrl_confirm_verified(vts_phase_controller_t *c, uint64_t id,int32_t phase,uint64_t now)
{
    if (!advance_time(c,now) || c->external_conflict || id==0U ||
        id!=c->pending_transaction_id) return false;
    if (!vts_fsm_commit_verified(&c->fsm,phase,c->active_ms)) return false;
    c->pending_transaction_id=0U;c->observed_phase=phase;return true;
}
bool vts_ctrl_mark_fault(vts_phase_controller_t *c,uint64_t id,uint64_t now)
{
    if (!advance_time(c,now) || id==0U || id!=c->pending_transaction_id ||
        c->fsm.mode!=VTS_FSM_PENDING) return false;
    vts_fsm_mark_fault(&c->fsm);c->single_candidate=false;return true;
}
bool vts_ctrl_resume_pending(vts_phase_controller_t *c,uint64_t id,uint64_t now)
{
    if (!advance_time(c,now) || c->external_conflict || id==0U ||
        id!=c->pending_transaction_id || c->fsm.mode!=VTS_FSM_FAULT) return false;
    c->fsm.mode=VTS_FSM_PENDING;return true;
}
bool vts_ctrl_observe_phase(vts_phase_controller_t *c,int32_t phase,uint64_t now)
{
    if (!advance_time(c,now)) return false;
    c->observed_phase=phase;
    if (c->fsm.mode==VTS_FSM_AWAIT_STARTUP) return c->external_conflict;
    bool plausible=phase==vts_fsm_current_phase(&c->fsm);
    if (c->pending_transaction_id!=0U) plausible=plausible || phase==c->fsm.pending.target_phase;
    /* Keep existing Python ownership while a saved Permanent is confirmed. */
    if (c->fsm.mode==VTS_FSM_PERMANENT && !c->pending_transaction_id) plausible=true;
    if (!plausible) {c->external_conflict=true;c->single_candidate=false;}
    return c->external_conflict;
}
uint64_t vts_ctrl_elapsed_ms(const vts_phase_controller_t *c,uint64_t now)
{
    if (!c || now<c->wall_ms) return 0U;
    uint64_t active=c->active_ms;
    if (time_is_running(c)) active+=now-c->wall_ms;
    return vts_fsm_elapsed_ms(&c->fsm,active);
}
bool vts_ctrl_invariants_hold(const vts_phase_controller_t *c)
{
    if (!c || !vts_fsm_invariants_hold(&c->fsm) || c->active_ms>c->wall_ms ||
        c->fsm.phase_started_ms>c->active_ms ||
        (c->button_eligible && !c->button_down) ||
        (c->button_down && c->button_down_ms>c->wall_ms) ||
        c->next_transaction_id==0U ||
        (c->second_down && (!c->button_down || !c->button_eligible)) ||
        (c->single_candidate && (c->button_down || c->pending_transaction_id!=0U ||
                                 c->first_short_release_ms>c->wall_ms))) return false;
    bool pending=c->fsm.mode==VTS_FSM_PENDING || c->fsm.mode==VTS_FSM_FAULT;
    if (pending!=(c->pending_transaction_id!=0U)) return false;
    return c->pending_transaction_id<c->next_transaction_id;
}
