package org.example.phaseviewer

/** An immutable, validated receiver snapshot. Values are not phone-generated. */
data class VtsBleV12Packet(
    val flags: Int,
    val phaseIndex: Int,
    val impedanceState: ImpedanceState,
    val sequence: Int,
    val phaseDeg: Int?,
    val rejectedMask: Int,
    val testedMask: Int,
    val permanentMask: Int,
    val elapsedSeconds: Long,
    val impedanceKohm: Long?,
    // Existing v12 data-carrier name is kept to retain source/API compatibility.
    // v13 decoder supplies these additional ring identifiers; never infer from degree.
    val protocolVersion: Int = 12,
    val fineParentIndex: Int = 0
) {
    val fine: Boolean get() = protocolVersion == 13 && flags and 0x40 != 0
    val memberCount: Int get() = if (fine) 7 else 12
    val parentPhaseDeg: Int? get() = if (fine && fineParentIndex in 1..12)
        VtsBleV12.PHASES[fineParentIndex - 1] else null
    fun memberPhase(index: Int): Int {
        require(index in 0 until memberCount)
        return if (fine) VtsBleV13.finePhase(fineParentIndex, index) else VtsBleV12.PHASES[index]
    }
    val ringPhases: List<Int> get() = (0 until memberCount).map(::memberPhase)
    val permanent: Boolean get() = flags and 1 != 0
    val pending: Boolean get() = flags and 2 != 0
    val active: Boolean get() = flags and 4 != 0
    val conflict: Boolean get() = flags and 8 != 0
    val fault: Boolean get() = flags and 16 != 0
    val startup: Boolean get() = flags and 32 != 0
    fun rejected(index: Int): Boolean = index in 0 until memberCount && rejectedMask and (1 shl index) != 0
    fun tested(index: Int): Boolean = index in 0 until memberCount && testedMask and (1 shl index) != 0
    fun permanentAt(index: Int): Boolean = index in 0 until memberCount && permanentMask and (1 shl index) != 0
}

enum class ImpedanceState(val wireValue: Int) {
    NOT_READY(0), OK(1), DISABLED(2), NO_LINK(3), STALE(4), BAD_SCALE(5), OUT_OF_RANGE(6)
}

/** Exact protocol12 counterpart of the unchanged v1.12a receiver/Python decoder. */
object VtsBleV12 {
    const val COMPANY_ID = 0xFFFF
    const val VERSION = 12
    const val LENGTH = 24
    const val UNAVAILABLE = 0xFFFF_FFFFL
    val PHASES: List<Int> = listOf(-60, 120, 0, 180, 60, -120, -90, 90, -30, 150, 30, -150)
    private fun u8(b: ByteArray, i: Int) = b[i].toInt() and 0xFF
    private fun u16(b: ByteArray, i: Int) = u8(b, i) or (u8(b, i + 1) shl 8)
    private fun u32(b: ByteArray, i: Int): Long =
        (u8(b, i).toLong() or (u8(b, i + 1).toLong() shl 8) or
         (u8(b, i + 2).toLong() shl 16) or (u8(b, i + 3).toLong() shl 24))

    fun crc16(bytes: ByteArray, length: Int = bytes.size): Int {
        require(length in 0..bytes.size)
        var crc = 0xFFFF
        for (i in 0 until length) {
            crc = crc xor (u8(bytes, i) shl 8)
            repeat(8) {
                crc = if (crc and 0x8000 != 0) ((crc shl 1) xor 0x1021) and 0xFFFF
                      else (crc shl 1) and 0xFFFF
            }
        }
        return crc
    }

    /** Manufacturer ID is removed by Android. Reject malformed/old-version data. */
    fun decode(bytes: ByteArray): VtsBleV12Packet? {
        if (bytes.size != LENGTH || u8(bytes, 0) != VERSION) return null
        if (u16(bytes, 22) != crc16(bytes, 22)) return null
        val flags = u8(bytes, 1)
        val index = u8(bytes, 2)
        val state = ImpedanceState.values().firstOrNull { it.wireValue == u8(bytes, 3) } ?: return null
        val phase = u16(bytes, 6).toShort().toInt()
        val rejected = u16(bytes, 8)
        val tested = u16(bytes, 10)
        val permanent = u16(bytes, 12)
        val impedance = u32(bytes, 18)
        if (flags and 0xC0 != 0 || (rejected or tested or permanent) and 0xF000 != 0) return null
        if (rejected and (tested or permanent) != 0) return null
        if ((state == ImpedanceState.OK) != (impedance != UNAVAILABLE)) return null
        val startup = flags and 32 != 0
        if (startup) {
            if (index != 0 || phase != 32767 || permanent != 0 || flags and 1 != 0) return null
        } else {
            if (index !in 1..12 || PHASES[index - 1] != phase) return null
            val bit = 1 shl (index - 1)
            if (rejected and bit != 0 || permanent !in listOf(0, bit)) return null
            if ((flags and 1 != 0) != (permanent != 0)) return null
        }
        return VtsBleV12Packet(flags, index, state, u16(bytes, 4), if (startup) null else phase,
            rejected, tested, permanent, u32(bytes, 14), if (impedance == UNAVAILABLE) null else impedance)
    }

    fun phaseLabel(degrees: Int): String = (if (degrees > 0) "+" else "") + degrees + "°"
    fun timerLabel(seconds: Long): String {
        require(seconds in 0..UNAVAILABLE)
        return "${seconds / 60}:${(seconds % 60).toString().padStart(2, '0')}"
    }
}
