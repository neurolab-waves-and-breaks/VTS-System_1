#include "vts_phase_persist.h"
#include <stddef.h>
#include <string.h>

static uint16_t read16(const uint8_t *x)
{ return (uint16_t)((uint16_t)x[0] | ((uint16_t)x[1] << 8)); }
static uint32_t read32(const uint8_t *x)
{ return (uint32_t)x[0] | ((uint32_t)x[1] << 8) |
         ((uint32_t)x[2] << 16) | ((uint32_t)x[3] << 24); }
static void put16(uint8_t *x,uint16_t v)
{ x[0]=(uint8_t)v; x[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *x,uint32_t v)
{ x[0]=(uint8_t)v;x[1]=(uint8_t)(v>>8);x[2]=(uint8_t)(v>>16);x[3]=(uint8_t)(v>>24); }
static uint32_t crc32(const uint8_t *data,size_t len)
{
    uint32_t crc=UINT32_C(0xFFFFFFFF);
    for(size_t i=0;i<len;++i) {
        crc^=data[i];
        for(unsigned j=0;j<8;++j)
            crc=(crc>>1)^((crc&1U)?UINT32_C(0xEDB88320):0U);
    }
    return ~crc;
}
static bool bit_valid(uint8_t index,uint8_t count,uint16_t rej,uint16_t tested)
{
    const uint16_t all=(uint16_t)((1U<<count)-1U);
    return index<count && !((rej|tested)&(uint16_t)~all) && !(rej&tested) &&
           !(rej&(uint16_t)(1U<<index)) && rej!=all;
}
bool vts_persist_state_valid(const vts_persist_state_t *s)
{
    if (!s || (s->level!=VTS_LEVEL_COARSE && s->level!=VTS_LEVEL_FINE) ||
        !bit_valid(s->current_index,VTS_FSM_PHASE_COUNT,s->rejected_mask,s->tested_mask))
        return false;
    if (!s->fine_valid) {
        return s->level==VTS_LEVEL_COARSE && s->fine_parent_index==0U &&
               s->fine_current_index==0U && s->fine_rejected_mask==0U &&
               s->fine_tested_mask==0U && s->fine_generation==0U &&
               s->fine_coarse_generation==0U;
    }
    if (s->fine_parent_index>=VTS_FSM_PHASE_COUNT ||
        s->fine_coarse_generation!=s->generation ||
        !bit_valid(s->fine_current_index,VTS_FSM_FINE_COUNT,
                   s->fine_rejected_mask,s->fine_tested_mask)) return false;
    if (s->level==VTS_LEVEL_FINE && s->fine_parent_index!=s->current_index) return false;
    return true;
}
int32_t vts_persist_phase(const vts_persist_state_t *s)
{
    if (!vts_persist_state_valid(s)) return INT32_MIN;
    return s->level==VTS_LEVEL_FINE ?
        vts_fsm_fine_phase(s->fine_parent_index,s->fine_current_index) :
        vts_fsm_phase_at(s->current_index);
}
static void from_fsm(vts_persist_state_t *s,const vts_phase_fsm_t *f)
{
    *s=(vts_persist_state_t){0};
    s->current_index=f->current_index;s->rejected_mask=f->rejected_mask;
    s->tested_mask=f->tested_mask;s->generation=f->generation;
    s->level=f->level;s->fine_valid=f->fine_valid;
    if(f->fine_valid) {
        s->fine_parent_index=f->fine_parent_index;
        s->fine_current_index=f->fine_current_index;
        s->fine_rejected_mask=f->fine_rejected_mask;
        s->fine_tested_mask=f->fine_tested_mask;
        s->fine_generation=f->fine_generation;
        s->fine_coarse_generation=f->fine_coarse_generation;
    }
}
/* Byte-for-byte original v1.13f writer; used for fixture parity only.
 * Real experimental receiver writes v2 to avoid downgrade ambiguity. */
bool vts_persist_encode(const vts_phase_fsm_t *f,uint8_t out[VTS_PERSIST_SIZE])
{
    if(!f || !out || f->mode!=VTS_FSM_PERMANENT ||
       f->level!=VTS_LEVEL_COARSE || !vts_fsm_invariants_hold(f))return false;
    memset(out,0,VTS_PERSIST_SIZE);
    out[0]='V';out[1]='T';out[2]='S';out[3]='P';out[4]=1;out[5]=1;
    out[6]=f->current_index;
    put16(out+8,f->rejected_mask);put16(out+10,f->tested_mask);
    put16(out+12,f->permanent_mask);put32(out+16,f->generation);
    put32(out+20,crc32(out,20));return true;
}
bool vts_persist_decode(const uint8_t raw[VTS_PERSIST_SIZE],vts_persist_state_t *out)
{
    if(!raw || !out || raw[0]!='V'||raw[1]!='T'||raw[2]!='S'||raw[3]!='P'||
       raw[4]!=1U||raw[5]!=1U||raw[7]!=0U||raw[14]!=0U||raw[15]!=0U||
       read32(raw+20)!=crc32(raw,20))return false;
    vts_persist_state_t s={0};
    s.current_index=raw[6];s.rejected_mask=read16(raw+8);
    s.tested_mask=read16(raw+10);s.generation=read32(raw+16);
    if(!vts_persist_state_valid(&s)||
       read16(raw+12)!=(uint16_t)(1U<<s.current_index))return false;
    *out=s;return true;
}
bool vts_persist_encode_v2(const vts_phase_fsm_t *f,uint8_t out[VTS_PERSIST_V2_SIZE])
{
    if(!f || !out || f->mode!=VTS_FSM_PERMANENT || !vts_fsm_invariants_hold(f))return false;
    vts_persist_state_t s;from_fsm(&s,f);
    if(!vts_persist_state_valid(&s))return false;
    memset(out,0,VTS_PERSIST_V2_SIZE);
    out[0]='V';out[1]='T';out[2]='S';out[3]='P';out[4]=2U;out[5]=1U;
    out[6]=(uint8_t)s.level;out[7]=s.fine_valid?s.fine_parent_index:0xFFU;
    put16(out+8,s.rejected_mask);put16(out+10,s.tested_mask);
    put32(out+12,s.generation);
    out[16]=s.fine_valid?s.fine_current_index:0xFFU;
    out[17]=(uint8_t)s.fine_rejected_mask;
    out[18]=(uint8_t)s.fine_tested_mask;
    out[19]=s.current_index;
    put32(out+20,s.fine_generation);put32(out+24,s.fine_coarse_generation);
    put16(out+28,(uint16_t)(int16_t)vts_fsm_current_phase(f));
    put32(out+32,crc32(out,32));
    return true;
}
bool vts_persist_decode_any(const uint8_t *raw,size_t len,vts_persist_state_t *out)
{
    if(!raw||!out)return false;
    if(len==VTS_PERSIST_SIZE)return vts_persist_decode(raw,out);
    if(len!=VTS_PERSIST_V2_SIZE || raw[0]!='V'||raw[1]!='T'||
       raw[2]!='S'||raw[3]!='P'||raw[4]!=2U||raw[5]!=1U ||
       raw[6]>1U || raw[19]>=VTS_FSM_PHASE_COUNT ||
       raw[30]!=0U || raw[31]!=0U || read32(raw+32)!=crc32(raw,32))return false;
    vts_persist_state_t s={0};
    s.level=(vts_fsm_level_t)raw[6];
    s.current_index=raw[19];s.rejected_mask=read16(raw+8);
    s.tested_mask=read16(raw+10);s.generation=read32(raw+12);
    if(raw[7]==0xFFU) {
        if(raw[16]!=0xFFU || raw[17]!=0U || raw[18]!=0U ||
           read32(raw+20)!=0U || read32(raw+24)!=0U) return false;
    } else {
        if(raw[7]>=VTS_FSM_PHASE_COUNT || raw[16]>=VTS_FSM_FINE_COUNT)return false;
        s.fine_valid=true;s.fine_parent_index=raw[7];
        s.fine_current_index=raw[16];s.fine_rejected_mask=raw[17];
        s.fine_tested_mask=raw[18];s.fine_generation=read32(raw+20);
        s.fine_coarse_generation=read32(raw+24);
    }
    if(!vts_persist_state_valid(&s) ||
       (int16_t)read16(raw+28)!=vts_persist_phase(&s))return false;
    *out=s;return true;
}
