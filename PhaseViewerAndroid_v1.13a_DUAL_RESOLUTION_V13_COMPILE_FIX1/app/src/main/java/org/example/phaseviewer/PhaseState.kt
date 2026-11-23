package org.example.phaseviewer

/** No live values are persisted across stops. The receiver remains authoritative. */
data class PhaseState(
    val scanning: Boolean = false,
    val fresh: Boolean = false,
    val status: String = "Not scanning",
    val packet: VtsBleV12Packet? = null,
    // Android monotonic timestamp of the last ACCEPTED, CRC-valid receiver snapshot.
    // Not updated by duplicated packets or phone-side display ticks.
    val receivedMonotonicMs: Long? = null
)
