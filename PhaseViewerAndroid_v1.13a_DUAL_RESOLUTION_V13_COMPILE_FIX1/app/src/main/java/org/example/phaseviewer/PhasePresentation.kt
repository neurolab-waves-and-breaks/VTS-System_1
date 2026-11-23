package org.example.phaseviewer

/** Semantic colours. Only the receiver's masks determine phase history. */
enum class PhaseRole { REMOVED, UNTESTED, TESTED, CURRENT, PERMANENT }
object PhasePresentation {
    const val FOOTER = "v1.13a · dual-resolution viewer"
    const val IMPEDANCE_TITLE = "EEG Impedance"
    const val WAITING = "VTS signal lost — off or out of range (last phases shown)"
    val LEGEND: List<Pair<PhaseRole, String>> = listOf(
        PhaseRole.CURRENT to "Current", PhaseRole.PERMANENT to "Permanent",
        PhaseRole.UNTESTED to "Untested", PhaseRole.TESTED to "Tested"
    )
    fun role(packet: VtsBleV12Packet?, index: Int): PhaseRole {
        require(index in 0 until (packet?.memberCount ?: 12))
        return when {
            packet?.rejected(index) == true -> PhaseRole.REMOVED
            packet?.permanentAt(index) == true -> PhaseRole.PERMANENT
            packet?.phaseIndex == index + 1 -> PhaseRole.CURRENT
            packet?.tested(index) == true -> PhaseRole.TESTED
            else -> PhaseRole.UNTESTED
        }
    }
    /** The selected number appears once, in the centre, not again on the rim. */
    fun rimLabel(packet: VtsBleV12Packet?, index: Int): String? =
        if (role(packet, index) == PhaseRole.REMOVED || packet?.phaseIndex == index + 1) null
        else VtsBleV12.phaseLabel(packet?.memberPhase(index) ?: VtsBleV12.PHASES[index])
    fun ringTitle(packet: VtsBleV12Packet?): String = when {
        packet?.fine == true -> "Fine selection · ±15° around ${VtsBleV12.phaseLabel(packet.parentPhaseDeg!!)} · 5° steps"
        else -> "Coarse selection · 12 phases"
    }
    fun selectedPhaseNote(packet: VtsBleV12Packet?): String? =
        if (packet?.permanent == true) "VTS saved selection · Python may temporarily set a different CLIC phase" else null
    fun phaseText(packet: VtsBleV12Packet?) = packet?.phaseDeg?.let(VtsBleV12::phaseLabel) ?: "—"
    fun timerText(packet: VtsBleV12Packet?) =
        if (packet == null || packet.startup) "—:—" else VtsBleV12.timerLabel(packet.elapsedSeconds)
    /** A real rounded zero is permitted. Never turn unavailable into zero. */
    fun impedanceText(state: PhaseState): String =
        if (state.fresh && state.packet?.impedanceState == ImpedanceState.OK &&
            state.packet.impedanceKohm != null && !state.packet.fault && !state.packet.conflict)
            "${state.packet.impedanceKohm} kΩ" else "— kΩ"
    fun statusText(state: PhaseState): String = when {
        !state.scanning -> state.status
        state.packet == null -> "Searching for VTS"
        !state.fresh -> WAITING
        state.packet.fault || state.packet.conflict -> "Device needs attention"
        state.packet.startup -> "Waiting for device"
        state.packet.pending -> "Changing phase…"
        !state.packet.active -> "CLIC inactive"
        else -> "Receiving updates"
    }
}
