package org.example.phaseviewer

/** Pure Kotlin receive/freshness policy, independent of Android or the UI. */
class VtsSnapshotTracker {
    companion object {
        // VTS broadcasts every 5 s; 12 s tolerates one lost interval, while
        // avoiding the original 15 s stale display. This is not an OFF signal.
        const val STALE_MS = 12_000L
        private const val REBOOT_CANDIDATE_MS = 8_000L
        private const val EARLY_REBOOT_MAX_SEQ = 16
        private const val REBOOT_MAX_STEP = 3
    }
    var packet: VtsBleV12Packet? = null
        private set
    var address: String? = null
        private set
    private var lastNewMs: Long? = null
    private var lastObservedMs: Long = -1L
    private var candidate: VtsBleV12Packet? = null
    private var candidateObservedMs: Long = -1L

    fun reset() {
        packet = null; address = null; lastNewMs = null; lastObservedMs = -1
        candidate = null; candidateObservedMs = -1
    }
    fun isFresh(nowMs: Long): Boolean = lastNewMs?.let { nowMs >= it && nowMs - it < STALE_MS } == true

    /**
     * Protocol v12/v13 has no boot ID, so a backwards sequence is never accepted
     * on a single advertisement. Two recently observed, INCREASING small-seq
     * packets from the bound BLE address corroborate a restart even while
     * old data is fresh. The first/second verified startup transmissions are
     * spaced ~850 ms by v1.13f/v1.14a; the regular 5 s stream is a fallback.
     * An unknown older counter is only eligible after stale. Duplicates,
     * queued old Android timestamps and other BLE devices cannot refresh.
     * This is a conservative heuristic, not authentication.
     */
    fun offer(next: VtsBleV12Packet, fromAddress: String, observedMs: Long, nowMs: Long): Boolean {
        if (fromAddress.isBlank() || observedMs < 0 || observedMs > nowMs ||
            nowMs - observedMs >= STALE_MS) return false
        if (address != null && address != fromAddress) return false
        if (observedMs <= lastObservedMs) return false
        val old = packet
        if (old == null) return commit(next, fromAddress, observedMs)
        val delta = (next.sequence - old.sequence) and 0xFFFF
        val c = candidate
        if (c != null && c.protocolVersion == next.protocolVersion && observedMs > candidateObservedMs &&
            observedMs - candidateObservedMs <= REBOOT_CANDIDATE_MS) {
            val step = (next.sequence - c.sequence) and 0xFFFF
            // If old seq is repeated, do not accept it unless a genuine low
            // counter candidate preceded it (e.g. old=2, new=1 then 2).
            val lowCounter = c.sequence <= EARLY_REBOOT_MAX_SEQ &&
                next.sequence <= EARLY_REBOOT_MAX_SEQ
            if (step in 1..REBOOT_MAX_STEP &&
                (next.protocolVersion == old.protocolVersion || lowCounter || !isFresh(nowMs)) &&
                (next.sequence != old.sequence || lowCounter))
                return commit(next, fromAddress, observedMs)
        }
        // Reflashing/rollback can change the protocol at the same BLE MAC.
        // Require two corroborating packets from the new protocol, never
        // a single frame that could be delayed from a previous scan session.
        if (next.protocolVersion != old.protocolVersion) {
            if (next.sequence <= EARLY_REBOOT_MAX_SEQ || !isFresh(nowMs)) {
                candidate = next; candidateObservedMs = observedMs
            }
            return false
        }
        if (delta == 0) return false
        if (delta < 0x8000) return commit(next, fromAddress, observedMs)
        // Backwards counter: an early new boot may be corroborated immediately.
        // More ambiguous counters require stale data before re-baselining.
        if (!isFresh(nowMs) || next.sequence <= EARLY_REBOOT_MAX_SEQ) {
            candidate = next; candidateObservedMs = observedMs
        }
        return false
    }
    private fun commit(next: VtsBleV12Packet, from: String, observedMs: Long): Boolean {
        packet = next; address = from; lastNewMs = observedMs; lastObservedMs = observedMs
        candidate = null; candidateObservedMs = -1
        return true
    }
}
