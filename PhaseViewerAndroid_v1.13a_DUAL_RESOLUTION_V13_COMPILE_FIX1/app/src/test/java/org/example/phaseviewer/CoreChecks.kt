package org.example.phaseviewer

import java.io.File
import kotlin.math.abs

/** Shared executable tests: JUnit on Android Gradle, plain JVM in this audit. */
object CoreChecks {
    var assertions = 0
        private set
    private fun verify(ok: Boolean, message: String) { assertions++; check(ok) { message } }
    fun hex(s: String) = s.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    private fun fixCrc(b: ByteArray): ByteArray {
        val crc = VtsBleV12.crc16(b, 22); b[22] = crc.toByte(); b[23] = (crc ushr 8).toByte(); return b
    }
    private fun put16(b: ByteArray, offset: Int, v: Int) { b[offset] = v.toByte(); b[offset + 1] = (v ushr 8).toByte() }
    private fun put32(b: ByteArray, offset: Int, v: Long) { repeat(4) { b[offset + it] = (v ushr (8 * it)).toByte() } }
    private fun base() = hex("0C04050123003C000700080000000A00000085010000F691")
    private fun packet(seq: Int = 35, elapsed: Long = 10L): VtsBleV12Packet =
        VtsBleV12.decode(fixCrc(base().also { put16(it, 4, seq); put32(it, 14, elapsed) }))!!

    val realHex = listOf(
        "0C0401011400C4FF00000000000065000000A3010000599D",
        "0C0402011800780001000000000000000000010000006242",
        "0C0402011900780001000000000005000000A3010000C3CA",
        "0C0402011A0078000100000000000A0000009F010000A870",
        "0C0403011C00000003000000000003000000AF0100006E3E",
        "0C0404011D00B40007000000000001000000A0010000160D",
        "0C0504011E00B40007000000080009000000A0010000038C",
        "0C0504012000B400070000000800130000008C0100001CAE",
        "0C04050123003C000700080000000A00000085010000F691"
    )
    fun runAll(): Int {
        assertions = 0
        verify(VtsBleV12.crc16("123456789".toByteArray()) == 0x29B1, "CRC standard check")
        val expectedPhases = listOf(-60,120,120,120,0,180,180,180,60)
        val expectedSeconds = listOf(101L,0L,5L,10L,3L,1L,9L,19L,10L)
        val expectedImpedance = listOf(419L,1L,419L,415L,431L,416L,416L,396L,389L)
        realHex.forEachIndexed { i, h ->
            val bytes = hex(h); val p = VtsBleV12.decode(bytes)
            verify(p != null, "Real captured packet $i")
            verify(p!!.phaseDeg == expectedPhases[i], "Signed phase $i")
            verify(p.elapsedSeconds == expectedSeconds[i], "Elapsed $i")
            verify(p.impedanceKohm == expectedImpedance[i], "Impedance $i")
            for (bit in 0 until bytes.size * 8) {
                val corrupt = bytes.copyOf(); corrupt[bit / 8] = (corrupt[bit / 8].toInt() xor (1 shl (bit % 8))).toByte()
                verify(VtsBleV12.decode(corrupt) == null, "Corrupt bit $bit accepted")
            }
        }
        // Wrong length is rejected before any indexing, including empty and oversized payloads.
        for (length in 0..65535) if (length != 24)
            verify(VtsBleV12.decode(ByteArray(length)) == null, "Length $length")
        val valid = base()
        fun invalid(label: String, edit: (ByteArray) -> Unit) {
            val b = valid.copyOf(); edit(b)
            verify(VtsBleV12.decode(fixCrc(b)) == null, label)
        }
        invalid("v9 rejected") { it[0] = 9 }
        invalid("Reserved flags rejected") { it[1] = 0x44 }
        invalid("Unknown impedance state") { it[3] = 7 }
        invalid("Wrong phase index") { it[2] = 6 }
        invalid("High rejected bits") { put16(it, 8, 0x8007) }
        invalid("High tested bits") { put16(it, 10, 0x1008) }
        invalid("Rejected/tested collision") { put16(it, 10, 9) }
        invalid("Current rejected") { put16(it, 8, 0x17) }
        invalid("Permanent bit without mask") { it[1] = 5 }
        invalid("Permanent on other phase") { it[1] = 5; put16(it, 12, 8) }
        invalid("Valid state with unavailable sentinel") { put32(it, 18, 0xFFFF_FFFFL) }
        invalid("Unavailable state with numeric value") { it[3] = 4 }
        invalid("Unconfirmed startup without sentinel") { it[1] = 32 }
        val unsigned = valid.copyOf().also { put32(it, 14, 0xFFFF_FFFFL); put32(it,18,0xFFFF_FFFEL) }
        val u = VtsBleV12.decode(fixCrc(unsigned))!!
        verify(u.elapsedSeconds == 4294967295L && u.impedanceKohm == 4294967294L, "Unsigned32")
        verify(VtsBleV12.timerLabel(u.elapsedSeconds) == "71582788:15", "Unsigned timer")
        val unknown = valid.copyOf().also { it[3]=3; put32(it,18,0xFFFF_FFFFL) }
        verify(VtsBleV12.decode(fixCrc(unknown))!!.impedanceKohm == null, "Unavailable null")
        val startup = valid.copyOf().also { it[1]=32;it[2]=0;put16(it,6,32767);put16(it,12,0) }
        verify(VtsBleV12.decode(fixCrc(startup))!!.phaseDeg == null, "Startup unknown phase")
        val zero = valid.copyOf().also { put32(it,18,0) }
        verify(PhasePresentation.impedanceText(PhaseState(true,true,packet=VtsBleV12.decode(fixCrc(zero)))) == "0 kΩ", "Real zero is not missing")

        val current = packet()
        verify(PhasePresentation.role(current,0) == PhaseRole.REMOVED, "Rejection hidden")
        verify(PhasePresentation.role(current,3) == PhaseRole.TESTED, "Old Permanent is Tested")
        verify(PhasePresentation.role(current,4) == PhaseRole.CURRENT, "Current highlight")
        verify(PhasePresentation.role(current,5) == PhaseRole.UNTESTED, "Untested preserved")
        verify(PhasePresentation.rimLabel(current,0) == null, "No removed text")
        verify(PhasePresentation.rimLabel(current,4) == null, "No duplicate selected value")
        verify(PhasePresentation.rimLabel(current,3) == "+180°", "Other label retained")
        val permanent = VtsBleV12.decode(hex(realHex[6]))!!
        verify(PhasePresentation.role(permanent,3) == PhaseRole.PERMANENT, "Permanent yellow role")
        verify(PhasePresentation.LEGEND.map { it.second } == listOf("Current","Permanent","Untested","Tested"), "Legend terminology")
        verify(PhasePresentation.phaseText(current) == "+60°", "Phase text")
        verify(PhasePresentation.timerText(packet(elapsed=150)) == "2:30", "Upward timer format")
        verify(PhasePresentation.timerText(packet(elapsed=245)) == "4:05", "No four-minute UI cutoff")
        verify(PhasePresentation.timerText(null) == "—:—", "No invented timer")
        verify(PhasePresentation.impedanceText(PhaseState(true,false,packet=current)) == "— kΩ", "Stale impedance hidden")
        verify(PhasePresentation.statusText(PhaseState(true,false,packet=current)) == PhasePresentation.WAITING, "Stale warning")
        verify(PhasePresentation.timerText(current) == "0:10", "Do not force missed-phase packet to zero")
        verify(PhasePresentation.timerText(permanent) == "0:09", "Do not reset on entering Permanent")
        for (i in 0..11) {
            val b = valid.copyOf(); b[2]=(i+1).toByte();put16(b,6,VtsBleV12.PHASES[i]);put16(b,8,0);put16(b,10,0)
            val q = VtsBleV12.decode(fixCrc(b))!!
            verify(q.phaseDeg == VtsBleV12.PHASES[i], "All phases including +180")
        }

        val tr=VtsSnapshotTracker()
        verify(tr.offer(packet(100),"A",1000,1000),"Initial bind")
        verify(!tr.offer(packet(101),"B",2000,2000),"Second device excluded")
        verify(!tr.offer(packet(100),"A",10000,10000),"Duplicate ignored")
        verify(tr.isFresh(12999) && !tr.isFresh(13000),"Duplicate does not refresh")
        verify(!tr.offer(packet(99),"A",500,16000),"Cached timestamp excluded")
        verify(!tr.offer(packet(99),"A",13000,13000),"First backwards stale candidate")
        verify(tr.offer(packet(100),"A",17000,17000) == false,"Same old seq cannot silently change")
        verify(tr.offer(packet(101),"A",18000,18000),"Fresh forward recovery")
        tr.reset()
        verify(tr.offer(packet(65535),"A",0,0),"Zero monotonic time supported")
        verify(tr.offer(packet(0),"A",10,10),"16-bit wrap")
        verify(!tr.offer(packet(65534),"A",20,20),"Old packet not rebound while fresh")
        verify(!tr.offer(packet(1),"A",0,30000),"Delayed old callback cannot refresh")
        verify(!tr.offer(packet(1),"A",31000,30000),"Future callback rejected")
        tr.reset();tr.offer(packet(1000),"A",1000,1000)
        verify(!tr.offer(packet(1),"A",1100,1100),"Reboot candidate alone insufficient")
        verify(tr.isFresh(1100),"Candidate cannot blank still-fresh old data")
        verify(tr.offer(packet(2),"A",1950,1950),"Reboot candidate corroborated promptly")
        verify(tr.packet!!.sequence==2 && tr.isFresh(1951),"Reboot updated display")
        tr.reset();verify(tr.packet==null && tr.address==null && !tr.isFresh(30000),"Stop clears binding/data")

        for (width in listOf(288f,320f,358f,379f,480f,520f)) for (font in listOf(1f,1.15f,1.3f,1.5f,2f)) {
            val h=minOf(width,320f)+(font-1f)*120f
            val effectiveFont=minOf(font,width*0.065f/18f)
            val w=58f*effectiveFont;val lh=23f*effectiveFont
            val layout=DialGeometry.layout(width,h,1f,w,lh)
            verify(layout.radius>0,"Positive ring")
            layout.positions.forEach { p ->
                verify(p.labelX-w/2 >= -0.01f && p.labelX+w/2 <= width+0.01f,"Label horizontal bounds")
                verify(p.labelY-lh/2 >= -0.01f && p.labelY+lh/2 <= h+0.01f,"Label vertical bounds")
            }
            // Every number uses <= max width, so bounding boxes are conservative.
            for (i in 0..11) for (j in i+1..11) {
                val a=layout.positions[i];val b=layout.positions[j]
                verify(abs(a.labelX-b.labelX)>=w || abs(a.labelY-b.labelY)>=lh,"Label collision w=$width font=$font i=$i j=$j")
            }
        }
        return assertions
    }
}
