#include "vts_telemetry_packet.h"
#include "vts_phase_fsm.h"
#include <string.h>
static void put16(uint8_t *b,uint16_t n){b[0]=(uint8_t)n;b[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *b,uint32_t n){for(unsigned i=0;i<4;++i)b[i]=(uint8_t)(n>>(8U*i));}
static uint16_t get16(const uint8_t *b){return(uint16_t)((uint16_t)b[0]|((uint16_t)b[1]<<8));}
static uint32_t get32(const uint8_t *b){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=(uint32_t)b[i]<<(8U*i);return n;}
uint16_t vts_telemetry_crc16(const uint8_t *data,size_t length)
{
    uint16_t crc=0xffffU;
    if(data==NULL && length!=0U)return 0U;
    for(size_t i=0;i<length;++i){
        crc^=(uint16_t)((uint16_t)data[i]<<8);
        for(unsigned j=0;j<8;++j)crc=(crc&0x8000U)?(uint16_t)((crc<<1)^0x1021U):(uint16_t)(crc<<1);
    }
    return crc;
}
static bool valid(const vts_telemetry_packet_t *s)
{
    if(!s || (s->flags & (uint8_t)~VTS_TF_ALL)!=0U || s->impedance_state>6U ||
       (s->impedance_state==1U)!=(s->impedance_kohm!=VTS_TELEMETRY_NA))return false;
    if ((s->rejected_mask & (s->tested_mask | s->permanent_mask))!=0U)return false;
    if (s->flags & VTS_TF_STARTUP) {
        return s->fine_parent_one_based==0U && s->index_one_based==0U &&
               s->phase_deg==VTS_TELEMETRY_PHASE_NA &&
               s->rejected_mask==0U && s->tested_mask==0U && s->permanent_mask==0U &&
               !(s->flags & (VTS_TF_PERMANENT|VTS_TF_FINE));
    }
    bool fine=(s->flags & VTS_TF_FINE)!=0U;
    if (fine) {
        if (s->fine_parent_one_based<1U || s->fine_parent_one_based>VTS_FSM_PHASE_COUNT ||
            s->index_one_based<1U || s->index_one_based>VTS_FSM_FINE_COUNT ||
            ((s->rejected_mask|s->tested_mask|s->permanent_mask) &
             (uint16_t)~VTS_FSM_ALL_FINE_MASK)!=0U ||
            s->phase_deg!=vts_fsm_fine_phase((uint8_t)(s->fine_parent_one_based-1U),
                                            (uint8_t)(s->index_one_based-1U)))return false;
    } else {
        if(s->fine_parent_one_based!=0U || s->index_one_based<1U ||
           s->index_one_based>VTS_FSM_PHASE_COUNT ||
           ((s->rejected_mask|s->tested_mask|s->permanent_mask) &
            (uint16_t)~VTS_FSM_ALL_PHASES_MASK)!=0U ||
           s->phase_deg!=vts_fsm_phase_at((uint8_t)(s->index_one_based-1U)))return false;
    }
    uint16_t current=(uint16_t)(1U<<(s->index_one_based-1U));
    if ((s->rejected_mask & current) ||
        (s->permanent_mask!=0U && s->permanent_mask!=current) ||
        (((s->flags & VTS_TF_PERMANENT)!=0U)!=(s->permanent_mask!=0U)))return false;
    return true;
}
bool vts_telemetry_encode(uint8_t out[VTS_TELEMETRY_BYTES],const vts_telemetry_packet_t *s)
{
    if(!out || !valid(s))return false;
    uint8_t b[VTS_TELEMETRY_BYTES]={0};
    b[0]=VTS_TELEMETRY_VERSION;
    b[1]=s->flags;
    b[2]=(uint8_t)((s->fine_parent_one_based<<4)|s->index_one_based);
    b[3]=s->impedance_state;put16(b+4,s->sequence);put16(b+6,(uint16_t)s->phase_deg);
    put16(b+8,s->rejected_mask);put16(b+10,s->tested_mask);put16(b+12,s->permanent_mask);
    put32(b+14,s->elapsed_seconds);put32(b+18,s->impedance_kohm);
    put16(b+22,vts_telemetry_crc16(b,22U));memcpy(out,b,sizeof b);return true;
}
bool vts_telemetry_decode(vts_telemetry_packet_t *out,const uint8_t *b,size_t length)
{
    if(!out || !b || length!=VTS_TELEMETRY_BYTES || b[0]!=VTS_TELEMETRY_VERSION ||
       get16(b+22)!=vts_telemetry_crc16(b,22U))return false;
    uint16_t u=get16(b+6);
    int32_t phase=u<=INT16_MAX?(int32_t)u:(int32_t)u-65536;
    vts_telemetry_packet_t s={.flags=b[1],.index_one_based=(uint8_t)(b[2]&0x0fU),
        .fine_parent_one_based=(uint8_t)(b[2]>>4),
        .impedance_state=b[3],.sequence=get16(b+4),.phase_deg=(int16_t)phase,
        .rejected_mask=get16(b+8),.tested_mask=get16(b+10),
        .permanent_mask=get16(b+12),.elapsed_seconds=get32(b+14),
        .impedance_kohm=get32(b+18)};
    if(!valid(&s))return false;
    *out=s;return true;
}
