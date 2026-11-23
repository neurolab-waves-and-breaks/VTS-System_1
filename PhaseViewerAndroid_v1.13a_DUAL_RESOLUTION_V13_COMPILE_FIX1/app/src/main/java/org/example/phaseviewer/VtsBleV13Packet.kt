package org.example.phaseviewer

/** Read-only decoder for VTS v1.14a BLE protocol 13. No advertising/CLIC commands. */
object VtsBleV13 {
    const val VERSION = 13
    const val LENGTH = 24
    private val OFFSETS = intArrayOf(-15, -10, -5, 0, 5, 10, 15)
    private fun u8(bytes: ByteArray, i: Int) = bytes[i].toInt() and 0xFF
    private fun u16(bytes: ByteArray, i: Int) = u8(bytes, i) or (u8(bytes, i + 1) shl 8)
    private fun u32(bytes: ByteArray, i: Int): Long =
        u8(bytes, i).toLong() or (u8(bytes, i + 1).toLong() shl 8) or
        (u8(bytes, i + 2).toLong() shl 16) or (u8(bytes, i + 3).toLong() shl 24)

    /** C firmware canonicalises (-180,180], so +180 is represented, not -180. */
    fun finePhase(parentOneBased: Int, memberZeroBased: Int): Int {
        require(parentOneBased in 1..12 && memberZeroBased in 0..6)
        val raw = VtsBleV12.PHASES[parentOneBased - 1] + OFFSETS[memberZeroBased]
        return ((raw + 179).mod(360)) - 179
    }

    /** Every field is checked against src/vts_telemetry_packet.c valid(). */
    fun decode(bytes: ByteArray): VtsBleV12Packet? {
        if (bytes.size != LENGTH || u8(bytes, 0) != VERSION) return null
        if (u16(bytes, 22) != VtsBleV12.crc16(bytes, 22)) return null
        val flags = u8(bytes, 1)
        if (flags and 0x80 != 0) return null
        val member = u8(bytes, 2) and 0x0F
        val parent = u8(bytes, 2) ushr 4
        val state = ImpedanceState.values().firstOrNull { it.wireValue == u8(bytes, 3) } ?: return null
        val seq = u16(bytes, 4)
        val phase = u16(bytes, 6).toShort().toInt()
        val rejected = u16(bytes, 8)
        val tested = u16(bytes, 10)
        val permanent = u16(bytes, 12)
        val elapsed = u32(bytes, 14)
        val imp = u32(bytes, 18)
        if ((state == ImpedanceState.OK) != (imp != VtsBleV12.UNAVAILABLE)) return null
        if (rejected and (tested or permanent) != 0) return null
        val startup = flags and 0x20 != 0
        val fine = flags and 0x40 != 0
        if (startup) {
            if (fine || flags and 1 != 0 || parent != 0 || member != 0 || phase != 32767 ||
                rejected != 0 || tested != 0 || permanent != 0) return null
        } else {
            if (fine) {
                if (parent !in 1..12 || member !in 1..7 || phase != finePhase(parent, member - 1)) return null
                if ((rejected or tested or permanent) and 0xFF80 != 0) return null
            } else {
                if (parent != 0 || member !in 1..12 || phase != VtsBleV12.PHASES[member - 1]) return null
                if ((rejected or tested or permanent) and 0xF000 != 0) return null
            }
            val currentBit = 1 shl (member - 1)
            if (rejected and currentBit != 0 || permanent !in listOf(0, currentBit)) return null
            if ((flags and 1 != 0) != (permanent != 0)) return null
        }
        return VtsBleV12Packet(
            flags, member, state, seq, if (startup) null else phase,
            rejected, tested, permanent, elapsed,
            if (imp == VtsBleV12.UNAVAILABLE) null else imp,
            protocolVersion = VERSION, fineParentIndex = parent
        )
    }
}

/** Version dispatch is explicit: never parse v13 bytes as v12 or vice versa. */
object VtsBleDual {
    const val COMPANY_ID = VtsBleV12.COMPANY_ID
    fun decode(bytes: ByteArray): VtsBleV12Packet? = when (bytes.firstOrNull()?.toInt()?.and(0xFF)) {
        VtsBleV12.VERSION -> VtsBleV12.decode(bytes)
        VtsBleV13.VERSION -> VtsBleV13.decode(bytes)
        else -> null
    }
}
