#ifndef VTS_PHASE_SELECTION_H
#define VTS_PHASE_SELECTION_H
#include <stdbool.h>
#include <stdint.h>

/* User-approved fixed circular order. Indices and angles never disappear. */
#define VTS_PHASE_COUNT 12U
void phase_selection_reset(void);
bool phase_selection_values_valid(void);
int32_t phase_selection_current(void);
int32_t phase_selection_next(void);
uint8_t phase_selection_index(void);
bool phase_selection_contains(int32_t phase_deg);
bool phase_selection_is_accepted(void);
uint16_t phase_selection_rejected_mask(void);
uint16_t phase_selection_accepted_mask(void);
/* These mutate the decision state ONLY after CLIC confirmation and feedback. */
bool phase_selection_commit_advance(int32_t confirmed_next_phase);
bool phase_selection_accept_current(void);
#endif
