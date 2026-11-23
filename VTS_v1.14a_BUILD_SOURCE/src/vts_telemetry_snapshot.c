#include "vts_telemetry_snapshot.h"
#include <string.h>
#include <limits.h>
bool vts_telemetry_snapshot(const vts_phase_link_t *l,uint64_t now,
                            uint16_t sequence,vts_telemetry_packet_t *out)
{
    if(!l || !out || now<l->ctrl.wall_ms || !vts_link_invariants(l))return false;
    const vts_phase_fsm_t *f=&l->ctrl.fsm;
    vts_impedance_snapshot_t imp;vts_link_impedance_snapshot(l,now,&imp);
    uint64_t seconds=vts_ctrl_elapsed_ms(&l->ctrl,now)/1000U;
    vts_telemetry_packet_t s={.sequence=sequence,
        .phase_deg=(int16_t)vts_fsm_current_phase(f),
        .index_one_based=(uint8_t)(vts_fsm_active_index(f)+1U),
        .fine_parent_one_based=f->level==VTS_LEVEL_FINE ?
            (uint8_t)(f->fine_parent_index+1U) : 0U,
        .rejected_mask=vts_fsm_active_rejected(f),
        .tested_mask=vts_fsm_active_tested(f),
        .permanent_mask=vts_fsm_active_permanent(f),
        .elapsed_seconds=seconds>UINT32_MAX?UINT32_MAX:(uint32_t)seconds,
        .impedance_state=(uint8_t)imp.state,.impedance_kohm=imp.valid?imp.rounded_kohm:VTS_TELEMETRY_NA};
    if(vts_fsm_active_permanent(f))s.flags|=VTS_TF_PERMANENT;
    if(f->level==VTS_LEVEL_FINE)s.flags|=VTS_TF_FINE;
    if(vts_link_pending(l))s.flags|=VTS_TF_PENDING;
    if(l->ctrl.clic_active)s.flags|=VTS_TF_ACTIVE;
    if(l->state==VTS_LINK_CONFLICT)s.flags|=VTS_TF_CONFLICT;
    if(l->state==VTS_LINK_FAULT || l->state==VTS_LINK_EXHAUSTED)s.flags|=VTS_TF_FAULT;
    if(f->mode==VTS_FSM_AWAIT_STARTUP){
        s.flags|=VTS_TF_STARTUP;s.phase_deg=VTS_TELEMETRY_PHASE_NA;
        s.index_one_based=0U;s.fine_parent_one_based=0U;
    }
    uint8_t scratch[VTS_TELEMETRY_BYTES];
    if(!vts_telemetry_encode(scratch,&s))return false;
    *out=s;return true;
}
bool vts_telemetry_window_allowed(const vts_phase_link_t *l,uint64_t now,uint32_t ms)
{
    if(!l || now<l->ctrl.wall_ms || ms==0U || ms>=VTS_LINK_ACTIVITY_TIMEOUT_MS ||
       !vts_link_invariants(l) || l->state!=VTS_LINK_IDLE || l->startup ||
       vts_link_pending(l) || l->command_waiting || l->output_ready || l->feedback_ready ||
       l->raw_down || l->stable_down || l->ctrl.button_down ||
       l->ctrl.single_candidate ||
       (l->ctrl.fsm.mode!=VTS_FSM_CYCLING && l->ctrl.fsm.mode!=VTS_FSM_PERMANENT))return false;
    uint64_t guard=(uint64_t)ms+2U*VTS_LINK_SERVICE_MS;
    if(now>UINT64_MAX-guard)return false;
    uint64_t end=now+guard;
    /* Do not enter an advertising window just before a scheduled read or
     * phase deadline. During CLIC inactivity, reads cannot proceed anyway. */
    if(l->ctrl.clic_active){
        if(!l->have_rx || now<l->last_rx_ms ||
           now-l->last_rx_ms+guard>=VTS_LINK_ACTIVITY_TIMEOUT_MS)return false;
        if(l->next_idle_poll_ms<=end)return false;
        if(l->impedance_enabled && l->next_impedance_poll_ms<=end)return false;
        unsigned available=0;
        for(unsigned i=0;i<vts_fsm_active_count(&l->ctrl.fsm);++i)
            if(!(vts_fsm_active_rejected(&l->ctrl.fsm) & (1U<<i)))++available;
        if(l->ctrl.fsm.mode==VTS_FSM_CYCLING && available>1U){
            uint64_t elapsed=vts_ctrl_elapsed_ms(&l->ctrl,now);
            if(elapsed>=VTS_FSM_DWELL_MS || VTS_FSM_DWELL_MS-elapsed<=guard)return false;
        }
    }
    return true;
}
