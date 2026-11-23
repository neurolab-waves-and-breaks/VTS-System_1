#include "vts_phase_fsm.h"
#include <stddef.h>

/* DO NOT REORDER: original v1.13f 12-phase patient traversal. */
static const int32_t k_phases[VTS_FSM_PHASE_COUNT] = {
    -60, 120, 0, 180, 60, -120, -90, 90, -30, 150, 30, -150
};
static const int8_t k_offsets[VTS_FSM_FINE_COUNT] = {-15, -10, -5, 0, 5, 10, 15};
static uint16_t bit_for(uint8_t index) { return (uint16_t)(UINT16_C(1) << index); }
static uint8_t count_for(vts_fsm_level_t level)
{ return level == VTS_LEVEL_FINE ? VTS_FSM_FINE_COUNT : VTS_FSM_PHASE_COUNT; }
static uint16_t all_for(vts_fsm_level_t level)
{ return level == VTS_LEVEL_FINE ? VTS_FSM_ALL_FINE_MASK : VTS_FSM_ALL_PHASES_MASK; }
static int32_t canonical(int32_t x)
{
    while (x > 180) x -= 360;
    while (x <= -180) x += 360;
    return x;
}
int32_t vts_fsm_phase_at(uint8_t index)
{ return index < VTS_FSM_PHASE_COUNT ? k_phases[index] : INT32_MIN; }
int32_t vts_fsm_fine_phase(uint8_t parent, uint8_t member)
{
    if (parent >= VTS_FSM_PHASE_COUNT || member >= VTS_FSM_FINE_COUNT) return INT32_MIN;
    return canonical(k_phases[parent] + k_offsets[member]);
}
uint8_t vts_fsm_active_index(const vts_phase_fsm_t *s)
{ return s && s->level == VTS_LEVEL_FINE ? s->fine_current_index : s ? s->current_index : 0U; }
uint8_t vts_fsm_active_count(const vts_phase_fsm_t *s)
{ return s ? count_for(s->level) : 0U; }
uint16_t vts_fsm_active_rejected(const vts_phase_fsm_t *s)
{ return s ? (s->level == VTS_LEVEL_FINE ? s->fine_rejected_mask : s->rejected_mask) : 0U; }
uint16_t vts_fsm_active_tested(const vts_phase_fsm_t *s)
{ return s ? (s->level == VTS_LEVEL_FINE ? s->fine_tested_mask : s->tested_mask) : 0U; }
uint16_t vts_fsm_active_permanent(const vts_phase_fsm_t *s)
{ return s ? (s->level == VTS_LEVEL_FINE ? s->fine_permanent_mask : s->permanent_mask) : 0U; }
int32_t vts_fsm_current_phase(const vts_phase_fsm_t *s)
{
    if (!s || s->current_index >= VTS_FSM_PHASE_COUNT) return INT32_MIN;
    if (s->level == VTS_LEVEL_COARSE) return k_phases[s->current_index];
    if (s->level != VTS_LEVEL_FINE || !s->fine_valid) return INT32_MIN;
    return vts_fsm_fine_phase(s->fine_parent_index, s->fine_current_index);
}
void vts_fsm_init(vts_phase_fsm_t *s)
{
    if (!s) return;
    *s=(vts_phase_fsm_t){0};
    s->mode=VTS_FSM_AWAIT_STARTUP;
    s->level=VTS_LEVEL_COARSE;
    s->pending.action=VTS_ACT_NONE;
}
bool vts_fsm_startup_confirmed(vts_phase_fsm_t *s, int32_t phase, uint64_t now)
{
    if (!s || s->mode!=VTS_FSM_AWAIT_STARTUP || phase!=k_phases[0]) return false;
    s->mode=VTS_FSM_CYCLING; s->phase_started_ms=now;
    return vts_fsm_invariants_hold(s);
}
uint64_t vts_fsm_elapsed_ms(const vts_phase_fsm_t *s, uint64_t now)
{
    if (!s || s->mode==VTS_FSM_AWAIT_STARTUP || now<s->phase_started_ms) return 0U;
    return now-s->phase_started_ms;
}
static bool next_eligible(uint8_t current, uint8_t count, uint16_t mask, uint8_t *next)
{
    for (uint8_t d=1U; d<count; ++d) {
        uint8_t i=(uint8_t)((current+d)%count);
        if (!(mask & bit_for(i))) { *next=i; return true; }
    }
    return false;
}
static bool ring_valid(uint8_t current, uint16_t rejected, uint16_t tested,
                       uint16_t permanent, uint16_t all, uint8_t count)
{
    if (current>=count || ((rejected|tested|permanent) & (uint16_t)~all) ||
        (rejected & tested) || (rejected & bit_for(current)) ||
        rejected==all || (permanent && permanent!=bit_for(current))) return false;
    return true;
}
bool vts_fsm_invariants_hold(const vts_phase_fsm_t *s)
{
    if (!s || (s->level!=VTS_LEVEL_COARSE && s->level!=VTS_LEVEL_FINE) ||
        !ring_valid(s->current_index,s->rejected_mask,s->tested_mask,
                    s->permanent_mask,VTS_FSM_ALL_PHASES_MASK,VTS_FSM_PHASE_COUNT)) return false;
    if (!s->fine_valid) {
        if (s->level==VTS_LEVEL_FINE || s->fine_rejected_mask || s->fine_tested_mask ||
            s->fine_permanent_mask || s->fine_generation || s->fine_coarse_generation ||
            s->fine_current_index || s->fine_parent_index) return false;
    } else {
        if (s->fine_parent_index>=VTS_FSM_PHASE_COUNT ||
            s->fine_coarse_generation!=s->generation ||
            !ring_valid(s->fine_current_index,s->fine_rejected_mask,s->fine_tested_mask,
                        s->fine_permanent_mask,VTS_FSM_ALL_FINE_MASK,VTS_FSM_FINE_COUNT)) return false;
        /* A cached Fine context is allowed to have a DIFFERENT parent from
         * the current coarse pointer, but never while Fine is active. */
        if (s->level==VTS_LEVEL_FINE && s->fine_parent_index!=s->current_index) return false;
    }
    if (s->mode==VTS_FSM_AWAIT_STARTUP) {
        if (s->level!=VTS_LEVEL_COARSE || s->fine_valid) return false;
    }
    if (s->mode==VTS_FSM_PERMANENT) {
        if (vts_fsm_active_permanent(s)!=bit_for(vts_fsm_active_index(s)) ||
            s->pending.action!=VTS_ACT_NONE) return false;
    } else if (s->mode==VTS_FSM_CYCLING || s->mode==VTS_FSM_AWAIT_STARTUP) {
        if (s->permanent_mask || s->fine_permanent_mask || s->pending.action!=VTS_ACT_NONE)
            return false;
    } else if (s->mode==VTS_FSM_PENDING || s->mode==VTS_FSM_FAULT) {
        if (s->pending.action==VTS_ACT_NONE ||
            (s->pending_target_level!=VTS_LEVEL_COARSE &&
             s->pending_target_level!=VTS_LEVEL_FINE)) return false;
    } else return false;
    if (s->level==VTS_LEVEL_FINE && s->permanent_mask) return false;
    if (s->level==VTS_LEVEL_COARSE && s->fine_permanent_mask) return false;
    return true;
}
bool vts_fsm_propose(vts_phase_fsm_t *s, vts_fsm_event_t event,
                     uint64_t now, vts_fsm_request_t *out)
{
    if (!s || !out || !vts_fsm_invariants_hold(s) ||
        (s->mode!=VTS_FSM_CYCLING && s->mode!=VTS_FSM_PERMANENT)) return false;
    vts_fsm_request_t q={VTS_ACT_NONE,0,false};
    vts_fsm_level_t level=s->level, target_level=level;
    uint8_t current=vts_fsm_active_index(s), next=0U, parent=s->current_index;
    uint16_t mask=vts_fsm_active_rejected(s);
    uint8_t count=count_for(level);
    bool fresh_fine=false;
    if (s->mode==VTS_FSM_PERMANENT) {
        if (event!=VTS_EVT_LONG_PRESS) return false;
        if (!next_eligible(current,count,mask,&next)) {
            if ((level==VTS_LEVEL_FINE ? s->fine_generation : s->generation)==UINT32_MAX)
                return false;
            q.action=VTS_ACT_EXIT_PERMANENT_RESET;
            q.resets_all_phases_on_commit=true;
        } else q.action=VTS_ACT_EXIT_PERMANENT_ADVANCE;
    } else if (event==VTS_EVT_LONG_PRESS) {
        q.action=VTS_ACT_ENTER_PERMANENT;next=current;
    } else if (event==VTS_EVT_SHORT_PRESS) {
        uint16_t rejected=(uint16_t)(mask|bit_for(current));
        q.action=VTS_ACT_REJECT_ADVANCE;
        if (rejected==all_for(level)) {
            if ((level==VTS_LEVEL_FINE ? s->fine_generation : s->generation)==UINT32_MAX)
                return false;
            q.resets_all_phases_on_commit=true;
        } else if (!next_eligible(current,count,rejected,&next)) return false;
    } else if (event==VTS_EVT_TIMER) {
        if (vts_fsm_elapsed_ms(s,now)<VTS_FSM_DWELL_MS ||
            !next_eligible(current,count,mask,&next)) return false;
        q.action=VTS_ACT_AUTO_ADVANCE;
    } else if (event==VTS_EVT_DOUBLE_PRESS) {
        if (level==VTS_LEVEL_COARSE) {
            target_level=VTS_LEVEL_FINE;
            fresh_fine= !s->fine_valid || s->fine_parent_index!=s->current_index ||
                        s->fine_coarse_generation!=s->generation;
            next=fresh_fine ? 3U : s->fine_current_index;
            q.action=VTS_ACT_ENTER_FINE;
        } else {
            target_level=VTS_LEVEL_COARSE;
            next=s->current_index;
            q.action=VTS_ACT_EXIT_FINE;
        }
    } else return false;
    q.target_phase=target_level==VTS_LEVEL_COARSE ? k_phases[next] :
        vts_fsm_fine_phase(parent,next);
    if (q.target_phase==INT32_MIN) return false;
    s->pending=q;
    s->pending_target_level=target_level;
    s->pending_target_index=next;
    s->pending_parent_index=parent;
    s->pending_new_fine=fresh_fine;
    s->mode=VTS_FSM_PENDING;
    *out=q;
    return vts_fsm_invariants_hold(s);
}
bool vts_fsm_commit_verified(vts_phase_fsm_t *s, int32_t verified, uint64_t now)
{
    if (!s || s->mode!=VTS_FSM_PENDING || s->pending.action==VTS_ACT_NONE ||
        verified!=s->pending.target_phase || now<s->phase_started_ms ||
        !vts_fsm_invariants_hold(s)) return false;
    vts_phase_fsm_t n=*s;
    vts_fsm_action_t action=n.pending.action;
    uint8_t current=vts_fsm_active_index(&n);
    if (action==VTS_ACT_ENTER_FINE) {
        if (n.pending_target_level!=VTS_LEVEL_FINE ||
            n.pending_parent_index!=n.current_index) return false;
        if (n.pending_new_fine) {
            n.fine_valid=true;
            n.fine_parent_index=n.current_index;
            n.fine_current_index=3U;
            n.fine_rejected_mask=0U;
            n.fine_tested_mask=0U;
            n.fine_permanent_mask=0U;
            n.fine_generation=0U;
            n.fine_coarse_generation=n.generation;
        }
        n.fine_current_index=n.pending_target_index;
        n.level=VTS_LEVEL_FINE;
        n.phase_started_ms=now;n.mode=VTS_FSM_CYCLING;
    } else if (action==VTS_ACT_EXIT_FINE) {
        if (n.pending_target_level!=VTS_LEVEL_COARSE) return false;
        n.level=VTS_LEVEL_COARSE;n.phase_started_ms=now;n.mode=VTS_FSM_CYCLING;
    } else if (action==VTS_ACT_ENTER_PERMANENT) {
        if (n.level==VTS_LEVEL_FINE) n.fine_permanent_mask=bit_for(n.fine_current_index);
        else n.permanent_mask=bit_for(n.current_index);
        n.mode=VTS_FSM_PERMANENT;
    } else {
        uint16_t *rej=n.level==VTS_LEVEL_FINE ? &n.fine_rejected_mask : &n.rejected_mask;
        uint16_t *tested=n.level==VTS_LEVEL_FINE ? &n.fine_tested_mask : &n.tested_mask;
        uint16_t *perm=n.level==VTS_LEVEL_FINE ? &n.fine_permanent_mask : &n.permanent_mask;
        uint8_t *idx=n.level==VTS_LEVEL_FINE ? &n.fine_current_index : &n.current_index;
        uint32_t *gen=n.level==VTS_LEVEL_FINE ? &n.fine_generation : &n.generation;
        if (action==VTS_ACT_REJECT_ADVANCE) {
            if (n.pending.resets_all_phases_on_commit) {
                *rej=0U;*tested=0U;
                (*gen)++;
            } else {
                *rej|=bit_for(current);*tested&=(uint16_t)~bit_for(current);
            }
        } else if (action==VTS_ACT_EXIT_PERMANENT_RESET) {
            *rej=0U;*tested=bit_for(current);(*gen)++;
        } else if (action==VTS_ACT_AUTO_ADVANCE || action==VTS_ACT_EXIT_PERMANENT_ADVANCE) {
            *tested|=bit_for(current);
        } else return false;
        *idx=n.pending_target_index;*perm=0U;
        n.phase_started_ms=now;n.mode=VTS_FSM_CYCLING;
        if (n.level==VTS_LEVEL_COARSE && n.pending.resets_all_phases_on_commit) {
            /* The old Fine context belongs to the previous coarse round. */
            n.fine_valid=false;n.fine_parent_index=0U;n.fine_current_index=0U;
            n.fine_rejected_mask=0U;n.fine_tested_mask=0U;
            n.fine_permanent_mask=0U;n.fine_generation=0U;n.fine_coarse_generation=0U;
        } else if (n.level==VTS_LEVEL_COARSE && n.fine_valid &&
                   n.fine_coarse_generation!=n.generation) return false;
    }
    n.pending=(vts_fsm_request_t){VTS_ACT_NONE,0,false};
    n.pending_target_index=0U;n.pending_parent_index=0U;
    n.pending_target_level=VTS_LEVEL_COARSE;n.pending_new_fine=false;
    if (!vts_fsm_invariants_hold(&n) || vts_fsm_current_phase(&n)!=verified) return false;
    *s=n; return true;
}
void vts_fsm_mark_fault(vts_phase_fsm_t *s)
{ if (s && s->mode==VTS_FSM_PENDING) s->mode=VTS_FSM_FAULT; }
