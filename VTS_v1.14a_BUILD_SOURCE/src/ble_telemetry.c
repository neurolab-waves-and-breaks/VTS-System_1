/* v1.12a BENCH ONLY: bounded advertising windows, same radio as CLIC ESB.
 * Preserves v1.9a's proven-on-phone legacy advertising/MPSL approach, now
 * coordinated by the SINGLE main-thread ESB owner. Not a claim of lossless
 * concurrent stimulation: ESB cannot receive during the advertising window.
 */
#include "ble_telemetry.h"
#if defined(CONFIG_BT)
#if !defined(CONFIG_VTS_BLE_TELEMETRY) || !defined(CONFIG_ESB_MPSL_TIMESLOT)
#error "Use ble_telemetry.conf: explicit telemetry and MPSL are required"
#endif
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <esb.h>
#include <errno.h>
#include <string.h>
LOG_MODULE_REGISTER(vts_ble, LOG_LEVEL_INF);
BUILD_ASSERT(CONFIG_VTS_BLE_WINDOW_MS < 1000, "Window must be shorter than link inactivity grace");
BUILD_ASSERT(3U+2U+2U+VTS_TELEMETRY_BYTES == 31U, "Legacy advertising payload must fit 31 bytes");
static bool ready, advertising, radio_failed, telemetry_disabled, priority_pending;
static bool startup_first_pending, startup_followup_pending;
static uint64_t startup_followup_due_ms;
#define VTS_BLE_STARTUP_SECOND_MS 850U
static uint64_t next_ms, stop_ms, pause_ms, retry_after_ms;
/* A transient ESB pause contention must not spin every 10 ms. */
#define VTS_BLE_PRIORITY_RETRY_MS 100U
static uint16_t sequence;
static uint8_t manufacturer[2U+VTS_TELEMETRY_BYTES];
static void fatal_radio(const char *stage,int rc)
{
    radio_failed=true;telemetry_disabled=true;
    /* Do not later restart ESB behind a terminal FSM conflict/fault. */
    LOG_ERR("VTS_BLE RADIO_FAULT stage=%s rc=%d reset_required=1",stage,rc);
}
static bool resume_esb(void)
{
    int rc=esb_start_rx();
    if(rc!=0 && rc!=-EALREADY){fatal_radio("ESB_RESUME",rc);return false;}
    LOG_INF("VTS_BLE ESB_RESUMED seq=%u gap_ms=%llu",(unsigned int)sequence,
            (unsigned long long)((uint64_t)k_uptime_get()-pause_ms));
    return true;
}
int vts_ble_telemetry_init(void)
{
    ready=false;advertising=false;radio_failed=false;telemetry_disabled=false;
    priority_pending=false;retry_after_ms=0;sequence=0;
    startup_first_pending=false;startup_followup_pending=false;startup_followup_due_ms=0;
    int rc=bt_enable(NULL);if(rc!=0)return rc;
    ready=true;next_ms=(uint64_t)k_uptime_get()+CONFIG_VTS_BLE_INTERVAL_MS;
    LOG_INF("VTS_BLE READY protocol=%u interval_ms=%u window_ms=%u advertise_only=1 legacy_app_compatible=0",
        (unsigned int)VTS_TELEMETRY_VERSION,
        (unsigned int)CONFIG_VTS_BLE_INTERVAL_MS,(unsigned int)CONFIG_VTS_BLE_WINDOW_MS);
    return 0;
}
bool vts_ble_telemetry_busy(void){return advertising;}
bool vts_ble_telemetry_failed(void){return radio_failed;}
void vts_ble_telemetry_request_priority(void)
{
    if (!ready || telemetry_disabled || radio_failed) return;
    /* Coalesce repeated requests. Never call BLE or ESB APIs from this path. */
    priority_pending=true;
    LOG_INF("VTS_BLE PRIORITY_REQUEST t_ms=%llu",(unsigned long long)k_uptime_get());
}
void vts_ble_telemetry_request_startup_sync(void)
{
    if (!ready || telemetry_disabled || radio_failed) return;
    /* The caller is the single phase owner after CLIC has GET/SET/GET-verified
     * startup/restored Permanent. Do not perform any radio operation here. */
    startup_first_pending=true;
    startup_followup_pending=false;
    priority_pending=true;
    LOG_INF("VTS_BLE STARTUP_SYNC_REQUEST t_ms=%llu",(unsigned long long)k_uptime_get());
}
bool vts_ble_telemetry_due(uint64_t now)
{
    return ready && !telemetry_disabled && !advertising &&
        now>=retry_after_ms && (priority_pending ||
        (startup_followup_pending && now>=startup_followup_due_ms) || now>=next_ms);
}
void vts_ble_telemetry_service(bool abort_window,uint64_t now)
{
    if(!advertising || radio_failed || (!abort_window && now<stop_ms))return;
    int rc=bt_le_adv_stop();
    if(rc!=0 && rc!=-EALREADY){
        fatal_radio("ADV_STOP",rc);
        /* No unconditional ESB resume when BLE stop failed. The owner will
         * latch FAULT, stop ESB and require explicit reset. */
        return;
    }
    advertising=false;
    (void)resume_esb();
}
void vts_ble_telemetry_poll(const vts_telemetry_packet_t *snapshot,
                           bool allowed,uint64_t now,bool (*recheck)(void))
{
    if(!allowed || !snapshot || !recheck || !vts_ble_telemetry_due(now))return;
    const bool second_due=startup_followup_pending && now>=startup_followup_due_ms;
    const bool priority=priority_pending || second_due;
    const bool first_sync=startup_first_pending && priority_pending;
    vts_telemetry_packet_t s=*snapshot;s.sequence=(uint16_t)(sequence+1U);
    uint8_t b[VTS_TELEMETRY_BYTES];
    if(!vts_telemetry_encode(b,&s)){
        telemetry_disabled=true;LOG_ERR("VTS_BLE INVALID_SNAPSHOT telemetry_disabled=1");return;
    }
    /* Retry pause contention later; never discard a pending ACK payload. */
    next_ms=now+CONFIG_VTS_BLE_INTERVAL_MS;
    int rc=esb_stop_rx();
    if(rc!=0){
        if(priority)retry_after_ms=now+VTS_BLE_PRIORITY_RETRY_MS;
        LOG_WRN("VTS_BLE ESB_PAUSE_SKIPPED rc=%d",rc);return;
    }
    pause_ms=(uint64_t)k_uptime_get();
    if(!recheck()){
        if(priority)retry_after_ms=now+VTS_BLE_PRIORITY_RETRY_MS;
        LOG_INF("VTS_BLE WINDOW_ABORTED input_or_busy=1");
        (void)resume_esb();return;
    }
    manufacturer[0]=0xffU;manufacturer[1]=0xffU;
    memcpy(manufacturer+2,b,sizeof(b));
    const struct bt_data ad[]={
        BT_DATA_BYTES(BT_DATA_FLAGS,BT_LE_AD_NO_BREDR),
        BT_DATA(BT_DATA_MANUFACTURER_DATA,manufacturer,sizeof(manufacturer))};
    const struct bt_le_adv_param p=BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_USE_IDENTITY,BT_GAP_ADV_FAST_INT_MIN_1,BT_GAP_ADV_FAST_INT_MIN_1,NULL);
    rc=bt_le_adv_start(&p,ad,ARRAY_SIZE(ad),NULL,0);
    if(rc!=0){
        LOG_ERR("VTS_BLE ADV_START_FAILED rc=%d telemetry_disabled=1",rc);
        telemetry_disabled=true;(void)resume_esb();return;
    }
    advertising=true;sequence=s.sequence;
    priority_pending=false;retry_after_ms=0;
    if (first_sync) {
        startup_first_pending=false;
        startup_followup_pending=true;
        startup_followup_due_ms=pause_ms+VTS_BLE_STARTUP_SECOND_MS;
    } else if (second_due) {
        startup_followup_pending=false;
    }
    stop_ms=pause_ms+CONFIG_VTS_BLE_WINDOW_MS;
    LOG_INF("VTS_BLE ADV_START seq=%u phase=%d index=%u fine_parent=%u elapsed_s=%u rejected=0x%03X tested=0x%03X permanent=0x%03X imp_state=%u imp_kohm=%u window_ms=%u priority=%u startup_sync=%u",
        (unsigned int)sequence,(int)s.phase_deg,(unsigned int)s.index_one_based,
        (unsigned int)s.fine_parent_one_based,(unsigned int)s.elapsed_seconds,(unsigned int)s.rejected_mask,(unsigned int)s.tested_mask,
        (unsigned int)s.permanent_mask,(unsigned int)s.impedance_state,
        (unsigned int)s.impedance_kohm,(unsigned int)CONFIG_VTS_BLE_WINDOW_MS,priority ? 1U : 0U,
        first_sync ? 1U : (second_due ? 2U : 0U));
    /* One line per advertising window, never per EEG/stimulation packet.
     * This is the offered payload, NOT evidence that a phone received it. */
    char hex[2U*VTS_TELEMETRY_BYTES+1U];static const char digits[]="0123456789ABCDEF";
    for(unsigned i=0;i<VTS_TELEMETRY_BYTES;++i){hex[2*i]=digits[b[i]>>4];hex[2*i+1]=digits[b[i]&15U];}
    hex[sizeof(hex)-1U]='\0';
    LOG_INF("VTS_BLE PAYLOAD seq=%u hex=%s",(unsigned int)sequence,hex);
}
#else
int vts_ble_telemetry_init(void){return 0;}
bool vts_ble_telemetry_busy(void){return false;}
bool vts_ble_telemetry_failed(void){return false;}
bool vts_ble_telemetry_due(uint64_t now){(void)now;return false;}
void vts_ble_telemetry_request_priority(void){}
void vts_ble_telemetry_request_startup_sync(void){}
void vts_ble_telemetry_service(bool abort_window,uint64_t now){(void)abort_window;(void)now;}
void vts_ble_telemetry_poll(const vts_telemetry_packet_t *s,bool a,uint64_t n,bool(*r)(void))
{(void)s;(void)a;(void)n;(void)r;}
#endif
