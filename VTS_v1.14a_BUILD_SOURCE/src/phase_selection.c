#include "phase_selection.h"

static const int32_t phases[VTS_PHASE_COUNT] = {
    -60, 120, 0, 180, 60, -120, -90, 90, -30, 150, 30, -150
};
static uint8_t current_index;
static bool accepted;
static uint16_t rejected_mask;
static uint16_t accepted_mask;

bool phase_selection_values_valid(void)
{
    static const int32_t reference[VTS_PHASE_COUNT] = {
        -60, 120, 0, 180, 60, -120, -90, 90, -30, 150, 30, -150
    };
    for (uint8_t i = 0; i < VTS_PHASE_COUNT; i++) {
        if (phases[i] != reference[i] || phases[i] < -180 || phases[i] > 180) {
            return false;
        }
        for (uint8_t j = 0; j < i; j++) {
            if (phases[i] == phases[j]) return false;
        }
    }
    return true;
}

void phase_selection_reset(void)
{
    current_index = 0;
    accepted = false;
    rejected_mask = 0;
    accepted_mask = 0;
}
int32_t phase_selection_current(void) { return phases[current_index]; }
int32_t phase_selection_next(void) { return phases[(current_index + 1U) % VTS_PHASE_COUNT]; }
uint8_t phase_selection_index(void) { return current_index; }
bool phase_selection_is_accepted(void) { return accepted; }
uint16_t phase_selection_rejected_mask(void) { return rejected_mask; }
uint16_t phase_selection_accepted_mask(void) { return accepted_mask; }
bool phase_selection_contains(int32_t phase_deg)
{
    for (uint8_t i = 0; i < VTS_PHASE_COUNT; i++) {
        if (phases[i] == phase_deg) return true;
    }
    return false;
}
/* A short press rejects the currently confirmed phase and advances one step.
 * The phase remains in the circular list; the mask is only history for BLE.
 */
bool phase_selection_commit_advance(int32_t confirmed_next_phase)
{
    if (accepted || confirmed_next_phase != phase_selection_next()) return false;
    rejected_mask |= (uint16_t)(1U << current_index);
    current_index = (uint8_t)((current_index + 1U) % VTS_PHASE_COUNT);
    return true;
}
/* A long press accepts the currently confirmed phase and locks until reboot. */
bool phase_selection_accept_current(void)
{
    if (accepted) return false;
    accepted_mask |= (uint16_t)(1U << current_index);
    accepted = true;
    return true;
}
