package org.example.phaseviewer

/**
 * PHONE DISPLAY ONLY. VTS remains authoritative for the phase and its timer.
 * Because BLE snapshots can be missed, local seconds are estimates, not evidence
 * of uninterrupted evaluation. Limit estimation to seven seconds after a fresh
 * receiver snapshot; then freeze until a new, validated BLE snapshot arrives.
 *
 * Never touch the phase/history masks, trigger logic, or any radio scheduling.
 * Uses the same Android monotonic clock as ScanResult.timestampNanos (since boot).
 */
object PhoneTimer {
    const val MAX_ESTIMATE_MS = 7_000L

    data class Display(val seconds: Long?, val estimated: Boolean)

    fun present(state: PhaseState, nowMonotonicMs: Long): Display {
        val p = state.packet ?: return Display(null, false)
        if (p.startup) return Display(null, false)
        val anchor = state.receivedMonotonicMs ?: return Display(p.elapsedSeconds, false)
        val ageMs = nowMonotonicMs - anchor
        if (!state.scanning || ageMs < 0 || !p.active || p.pending || p.fault || p.conflict)
            return Display(p.elapsedSeconds, false)
        val extraSeconds = minOf(ageMs, MAX_ESTIMATE_MS) / 1_000L
        // VTS wire elapsed is uint32; never exceed the representable value.
        val displaySeconds = (p.elapsedSeconds + extraSeconds).coerceAtMost(VtsBleV12.UNAVAILABLE)
        return Display(displaySeconds, extraSeconds > 0L)
    }
}
