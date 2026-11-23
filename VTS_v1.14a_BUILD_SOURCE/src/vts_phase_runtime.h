#ifndef VTS_PHASE_RUNTIME_H
#define VTS_PHASE_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>
#include "vts_phase_persist.h"
/* Called by main only; main serializes all portable engine mutations. */
int vts_runtime_init(bool button_held);
void vts_runtime_start(void);
void vts_runtime_poll(void);
void vts_runtime_status(const char *label);
/* IRQ/thread producers: no blocking, no phase mutation, overflow fails closed. */
void vts_runtime_rx_activity(void);
void vts_runtime_ack(void);
void vts_runtime_response(const uint8_t *body, uint32_t length);
void vts_runtime_button_edge(bool pressed);
void vts_runtime_feedback_complete(uint32_t ticket, int play_rc, int restore_rc);
/* Implemented in main: retain existing DA7280 feedback thread. */
int vts_hw_submit_feedback(uint32_t ticket, uint16_t duration_ms);
/* Existing main-thread NVS storage, distinct record from haptic settings. */
int vts_hw_permanent_load(vts_persist_state_t *out); /* 1 saved; 0 absent; negative fault */
int vts_hw_permanent_save(const vts_phase_fsm_t *fsm);
int vts_hw_permanent_clear(void);
#endif
