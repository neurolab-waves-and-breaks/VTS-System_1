package org.example.phaseviewer

import kotlin.math.abs

/** Decode *actual* C v1.14a encoder vectors with a fully independent Kotlin decoder. */
object DualV13Checks {
    private var assertions = 0
    private fun verify(condition: Boolean, msg: String) { assertions++; check(condition) { msg } }
    private fun fixCrc(b: ByteArray): ByteArray {
        val crc = VtsBleV12.crc16(b, 22)
        b[22] = crc.toByte(); b[23] = (crc ushr 8).toByte()
        return b
    }
    private fun set16(b: ByteArray, offset: Int, n: Int) {
        b[offset] = n.toByte(); b[offset + 1] = (n ushr 8).toByte()
    }
    private fun set32(b: ByteArray, offset: Int, n: Long) {
        for (i in 0..3) b[offset + i] = (n ushr (8 * i)).toByte()
    }
    private val cVectors = listOf(
        "FINE 0 0 0D4411010100B5FF0000000000002A0000008701000072E1",
        "FINE 0 1 0D4412010200BAFF0000000000002A00000087010000CFC6",
        "FINE 0 2 0D4413010300BFFF0000000000002A00000087010000BB2B",
        "FINE 0 3 0D4414010400C4FF0000000000002A000000870100008943",
        "FINE 0 4 0D4415010500C9FF0000000000002A0000008701000078BF",
        "FINE 0 5 0D4416010600CEFF0000000000002A000000870100004089",
        "FINE 0 6 0D4417010700D3FF0000000000002A00000087010000BB56",
        "FINE 1 0 0D442101080069000000000000002A000000870100008A99",
        "FINE 1 1 0D44220109006E000000000000002A00000087010000F15F",
        "FINE 1 2 0D4423010A0073000000000000002A000000870100004970",
        "FINE 1 3 0D4424010B0078000000000000002A00000087010000A9F1",
        "FINE 1 4 0D4425010C007D000000000000002A00000087010000391C",
        "FINE 1 5 0D4426010D0082000000000000002A00000087010000802A",
        "FINE 1 6 0D4427010E0087000000000000002A00000087010000B737",
        "FINE 2 0 0D4431010F00F1FF0000000000002A00000087010000A5CA",
        "FINE 2 1 0D4432011000F6FF0000000000002A00000087010000AA0F",
        "FINE 2 2 0D4433011100FBFF0000000000002A000000870100005BF3",
        "FINE 2 3 0D443401120000000000000000002A00000087010000E891",
        "FINE 2 4 0D443501130005000000000000002A000000870100009C7C",
        "FINE 2 5 0D44360114000A000000000000002A0000008701000086AB",
        "FINE 2 6 0D44370115000F000000000000002A00000087010000F246",
        "FINE 3 0 0D4441011600A5000000000000002A000000870100003BE4",
        "FINE 3 1 0D4442011700AA000000000000002A00000087010000C533",
        "FINE 3 2 0D4443011800AF000000000000002A000000870100003A2F",
        "FINE 3 3 0D4444011900B4000000000000002A00000087010000D08D",
        "FINE 3 4 0D4445011A0051FF0000000000002A00000087010000FDA0",
        "FINE 3 5 0D4446011B0056FF0000000000002A000000870100008666",
        "FINE 3 6 0D4447011C005BFF0000000000002A00000087010000939A",
        "FINE 4 0 0D4451011D002D000000000000002A000000870100008167",
        "FINE 4 1 0D4452011E0032000000000000002A000000870100003663",
        "FINE 4 2 0D4453011F0037000000000000002A00000087010000428E",
        "FINE 4 3 0D44540120003C000000000000002A0000008701000009F9",
        "FINE 4 4 0D445501210041000000000000002A00000087010000CEEC",
        "FINE 4 5 0D445601220046000000000000002A00000087010000F6DA",
        "FINE 4 6 0D44570123004B000000000000002A000000870100000726",
        "FINE 5 0 0D446101240079FF0000000000002A00000087010000AD01",
        "FINE 5 1 0D44620125007EFF0000000000002A00000087010000D6C7",
        "FINE 5 2 0D446301260083FF0000000000002A00000087010000232A",
        "FINE 5 3 0D446401270088FF0000000000002A00000087010000C3AB",
        "FINE 5 4 0D44650128008DFF0000000000002A000000870100003CB7",
        "FINE 5 5 0D446601290092FF0000000000002A00000087010000C843",
        "FINE 5 6 0D4467012A0097FF0000000000002A00000087010000FF5E",
        "FINE 6 0 0D4471012B0097FF0000000000002A000000870100003778",
        "FINE 6 1 0D4472012C009CFF0000000000002A00000087010000FF2F",
        "FINE 6 2 0D4473012D00A1FF0000000000002A0000008701000010B6",
        "FINE 6 3 0D4474012E00A6FF0000000000002A00000087010000E456",
        "FINE 6 4 0D4475012F00ABFF0000000000002A0000008701000015AA",
        "FINE 6 5 0D4476013000B0FF0000000000002A0000008701000047DD",
        "FINE 6 6 0D4477013100B5FF0000000000002A000000870100003330",
        "FINE 7 0 0D44810132004B000000000000002A00000087010000A625",
        "FINE 7 1 0D448201330050000000000000002A000000870100008051",
        "FINE 7 2 0D448301340055000000000000002A0000008701000010BC",
        "FINE 7 3 0D44840135005A000000000000002A0000008701000022BD",
        "FINE 7 4 0D44850136005F000000000000002A0000008701000015A0",
        "FINE 7 5 0D448601370064000000000000002A000000870100002792",
        "FINE 7 6 0D448701380069000000000000002A000000870100005D9F",
        "FINE 8 0 0D4491013900D3FF0000000000002A000000870100004177",
        "FINE 8 1 0D4492013A00D8FF0000000000002A000000870100002ED0",
        "FINE 8 2 0D4493013B00DDFF0000000000002A000000870100005A3D",
        "FINE 8 3 0D4494013C00E2FF0000000000002A000000870100009259",
        "FINE 8 4 0D4495013D00E7FF0000000000002A00000087010000E6B4",
        "FINE 8 5 0D4496013E00ECFF0000000000002A000000870100008913",
        "FINE 8 6 0D4497013F00F1FF0000000000002A0000008701000072CC",
        "FINE 9 0 0D44A101400087000000000000002A00000087010000F2EA",
        "FINE 9 1 0D44A20141008C000000000000002A00000087010000DEBD",
        "FINE 9 2 0D44A301420091000000000000002A000000870100006692",
        "FINE 9 3 0D44A401430096000000000000002A00000087010000D182",
        "FINE 9 4 0D44A50144009B000000000000002A00000087010000C47E",
        "FINE 9 5 0D44A6014500A0000000000000002A00000087010000F64C",
        "FINE 9 6 0D44A7014600A5000000000000002A00000087010000C151",
        "FINE 10 0 0D44B10147000F000000000000002A000000870100008068",
        "FINE 10 1 0D44B201480014000000000000002A000000870100002DED",
        "FINE 10 2 0D44B301490019000000000000002A00000087010000DC11",
        "FINE 10 3 0D44B4014A001E000000000000002A0000008701000028F1",
        "FINE 10 4 0D44B5014B0023000000000000002A00000087010000C768",
        "FINE 10 5 0D44B6014C0028000000000000002A000000870100000F3F",
        "FINE 10 6 0D44B7014D002D000000000000002A000000870100007BD2",
        "FINE 11 0 0D44C1014E005BFF0000000000002A00000087010000E1B4",
        "FINE 11 1 0D44C2014F0060FF0000000000002A00000087010000D386",
        "FINE 11 2 0D44C301500065FF0000000000002A00000087010000D368",
        "FINE 11 3 0D44C40151006AFF0000000000002A00000087010000E169",
        "FINE 11 4 0D44C50152006FFF0000000000002A00000087010000D674",
        "FINE 11 5 0D44C601530074FF0000000000002A00000087010000F000",
        "FINE 11 6 0D44C701540079FF0000000000002A00000087010000E5FC",
        "COARSE 0 0D0401016400C4FF0000000000002A0000008701000093DE",
        "COARSE 1 0D040201650078000000000000002A00000087010000187E",
        "COARSE 2 0D040301660000000000000000002A000000870100006AB3",
        "COARSE 3 0D0404016700B4000000000000002A00000087010000FF26",
        "COARSE 4 0D04050168003C000000000000002A00000087010000020B",
        "COARSE 5 0D040601690088FF0000000000002A000000870100000CBA",
        "COARSE 6 0D0407016A00A6FF0000000000002A00000087010000E718",
        "COARSE 7 0D0408016B005A000000000000002A00000087010000315F",
        "COARSE 8 0D0409016C00E2FF0000000000002A00000087010000EA14",
        "COARSE 9 0D040A016D0096000000000000002A00000087010000BD21",
        "COARSE 10 0D040B016E001E000000000000002A00000087010000880D",
        "COARSE 11 0D040C016F006AFF0000000000002A0000008701000013EE"
    )
    fun runAll() {
        assertions = 0
        verify(cVectors.size == 96, "Expected all 96 independent C packets")
        cVectors.forEach { line ->
            val parts = line.split(" ")
            val hex = parts.last()
            val wire = CoreChecks.hex(hex)
            val p = VtsBleDual.decode(wire)
            verify(p != null && p.protocolVersion == 13, "C vector not decoded: $line")
            val packet = p!!
            if (parts[0] == "FINE") {
                val parent = parts[1].toInt(); val member = parts[2].toInt()
                verify(packet.fine && packet.memberCount == 7, "Fine flag and count: $line")
                verify(packet.fineParentIndex == parent + 1 && packet.phaseIndex == member + 1, "Packed parent/member: $line")
                verify(packet.phaseDeg == VtsBleV13.finePhase(parent + 1, member), "C angle parity: $line")
                verify(packet.parentPhaseDeg == VtsBleV12.PHASES[parent], "Parent angle identity")
                verify(packet.ringPhases.size == 7, "Fine cardinality")
            } else {
                verify(parts[0] == "COARSE", "Bad fixture label")
                val member = parts[1].toInt()
                verify(!packet.fine && packet.fineParentIndex == 0 && packet.memberCount == 12, "Coarse identity")
                verify(packet.phaseIndex == member + 1 && packet.phaseDeg == VtsBleV12.PHASES[member], "Coarse identity angle")
            }
            verify(packet.impedanceKohm == 391L && packet.elapsedSeconds == 42L && packet.active, "C payload fields")
            verify(VtsBleV12.decode(wire) == null, "v12 decoder must reject v13")
            verify(VtsBleV13.decode(wire) == packet, "v13 dispatch exact")
            val damaged = wire.copyOf(); damaged[4] = (damaged[4].toInt() xor 0x01).toByte()
            verify(VtsBleDual.decode(damaged) == null, "Bad CRC not rejected")
        }
        val fine90 = VtsBleDual.decode(CoreChecks.hex(cVectors.first { it.startsWith("FINE 7 3 ") }.split(" ").last()))!!
        verify(fine90.ringPhases == listOf(75, 80, 85, 90, 95, 100, 105), "90-degree neighbourhood")
        verify(PhasePresentation.ringTitle(fine90).contains("+90°") &&
               PhasePresentation.ringTitle(fine90).contains("5°"), "Fine title")
        val fine180 = VtsBleDual.decode(CoreChecks.hex(cVectors.first { it.startsWith("FINE 3 3 ") }.split(" ").last()))!!
        verify(fine180.ringPhases == listOf(165, 170, 175, 180, -175, -170, -165), "180-degree wraparound")
        val sameValueOtherParent = VtsBleDual.decode(CoreChecks.hex(cVectors.first { it.startsWith("FINE 4 0 ") }.split(" ").last()))!!
        verify(sameValueOtherParent.parentPhaseDeg == 60 && sameValueOtherParent.fineParentIndex != fine90.fineParentIndex,
            "Ring identity distinct from numerical angle")
        // Explicit masks must refer to seven fine members, never bits from the parent coarse ring.
        val sample = CoreChecks.hex(cVectors.first { it.startsWith("FINE 7 4 ") }.split(" ").last())
        val history = sample.copyOf().also { set16(it, 8, 0x008); set16(it, 10, 0x002); it[1] = 0x45; set16(it, 12, 0x010) }
        val permanent = VtsBleDual.decode(fixCrc(history))!!
        verify(permanent.fine && permanent.phaseDeg == 95 && permanent.permanent &&
               permanent.rejected(3) && permanent.tested(1) && permanent.permanentAt(4), "Fine Permanent masks")
        verify(PhasePresentation.role(permanent,3) == PhaseRole.REMOVED &&
               PhasePresentation.role(permanent,1) == PhaseRole.TESTED &&
               PhasePresentation.role(permanent,4) == PhaseRole.PERMANENT, "Fine UI mask role")
        verify(PhasePresentation.rimLabel(permanent,4) == null &&
               PhasePresentation.rimLabel(permanent,5) == "+100°", "Fine labels and selected number unique")
        verify(PhasePresentation.selectedPhaseNote(permanent)?.contains("Python") == true, "Python override caution")
        fun invalid(msg: String, edit: (ByteArray) -> Unit) {
            val b = sample.copyOf(); edit(b)
            verify(VtsBleDual.decode(fixCrc(b)) == null, msg)
        }
        invalid("Fine parent 0 illegal") { it[2] = 0x05 }
        invalid("Fine parent 13 illegal") { it[2] = 0xD5.toByte() }
        invalid("Fine member 8 illegal") { it[2] = 0x88.toByte() }
        invalid("Fine member 0 illegal") { it[2] = 0x80.toByte() }
        invalid("Fine mismatched actual angle") { set16(it, 6, 100) }
        invalid("Fine malformed 8th rejected bit") { set16(it, 8, 0x080) }
        invalid("Fine malformed 8th tested bit") { set16(it, 10, 0x080) }
        invalid("Fine malformed 8th permanent bit") { set16(it, 12, 0x080) }
        invalid("Fine flags without parent") { it[2] = 0x05 }
        invalid("Coarse nonzero parent") { it[1] = 4; it[2] = 0x15 }
        invalid("Fine current rejected") { set16(it, 8, 0x010) }
        invalid("Fine rejected overlaps tested") { set16(it, 8, 0x008); set16(it, 10, 0x008) }
        invalid("Fine permanent flag with empty permanent mask") { it[1] = 0x45 }
        invalid("Fine wrong permanent mask") { it[1] = 0x45; set16(it, 12, 0x008) }
        invalid("Reserved flag") { it[1] = 0xC4.toByte() }
        invalid("Unknown impedance state") { it[3] = 7 }
        invalid("OK impedance sentinel") { set32(it, 18, VtsBleV12.UNAVAILABLE) }
        invalid("Not-ready with numeric impedance") { it[3] = 0 }
        invalid("Startup cannot retain ring masks") { it[1] = 0x20; it[2] = 0; set16(it, 6, 32767); set16(it, 8, 1) }
        val startup = sample.copyOf().also {
            it[1] = 0x20; it[2] = 0; set16(it, 6, 32767); set16(it, 8, 0); set16(it, 10, 0); set16(it, 12, 0)
        }
        verify(VtsBleDual.decode(fixCrc(startup))?.startup == true, "New v13 startup packet")
        verify(VtsBleDual.decode(ByteArray(23)) == null && VtsBleDual.decode(ByteArray(25)) == null, "No truncated or extra bytes")
        verify(VtsBleDual.decode(CoreChecks.hex(CoreChecks.realHex[6]))?.protocolVersion == 12, "v12 actual payload retained")
        // Corroborated change in firmware protocol at the SAME MAC; reject a single version flip.
        val v12 = VtsBleV12.decode(CoreChecks.hex(CoreChecks.realHex[6]))!!
        val v13a = fine90.copy(sequence = 1)
        val v13b = fine90.copy(sequence = 2)
        val tr = VtsSnapshotTracker()
        verify(tr.offer(v12.copy(sequence=220), "same-mac", 1000, 1000), "Initial v12")
        verify(!tr.offer(v13a, "same-mac", 1900, 1900), "No single-frame protocol promotion")
        verify(tr.packet!!.protocolVersion == 12 && tr.isFresh(1900), "Old snapshot remains until corroboration")
        verify(!tr.offer(v13a, "same-mac", 2000, 2000), "Duplicate not corroboration")
        verify(tr.offer(v13b, "same-mac", 2750, 2750), "Two v13 advertisements corroborate")
        verify(tr.packet!!.fine && tr.packet!!.protocolVersion == 13, "V13 ring now active")
        verify(!tr.offer(v12.copy(sequence=221), "same-mac", 3100, 3100), "One queued old v12 frame cannot downgrade")
        verify(tr.packet!!.protocolVersion == 13, "No mixed protocol regress")
        verify(tr.offer(v13b.copy(sequence=3), "same-mac", 3900, 3900), "Current v13 stream continues")
        tr.reset()
        verify(tr.offer(v13a, "A", 1000, 1000), "Fresh v13 boot")
        verify(!tr.offer(v12.copy(sequence=1), "A", 1900, 1900), "No single v12 rollback")
        verify(tr.offer(v12.copy(sequence=2), "A", 2750, 2750), "Two v12 rollback packets")
        verify(tr.packet!!.protocolVersion == 12, "Downgrade to working v12 works")
        tr.reset(); verify(tr.offer(v13a, "A", 1000, 1000), "V13 bind")
        verify(!tr.offer(v13b, "B", 1850, 1850), "Other MAC ignored")
        verify(!tr.offer(v13a, "A", 1900, 1900), "Duplicate ignored")
        verify(tr.offer(v13b, "A", 2750, 2750), "Same MAC forward accepted")
        for (width in listOf(288f, 320f, 358f, 379f, 480f, 520f)) for (font in listOf(1f, 1.15f, 1.3f, 1.5f, 2f)) {
            val h = minOf(width, 320f) + (font-1f)*120f
            val effectiveFont = minOf(font,width*0.065f/18f)
            val w = 61f*effectiveFont; val lh = 23f*effectiveFont
            val lay = DialGeometry.layoutFine(width,h,1f,w,lh)
            verify(lay.positions.size == 7 && lay.radius > 0, "Fine node count/radius")
            lay.positions.forEach { q ->
                verify(q.labelX-w/2 >= -0.01f && q.labelX+w/2 <= width+0.01f, "Fine label X bounds")
                verify(q.labelY-lh/2 >= -0.01f && q.labelY+lh/2 <= h+0.01f, "Fine label Y bounds")
            }
            for (i in 0..6) for (j in i+1..6) {
                val a=lay.positions[i];val b=lay.positions[j]
                verify(abs(a.labelX-b.labelX)>=w || abs(a.labelY-b.labelY)>=lh,
                    "Fine label collision at width=$width font=$font members=$i,$j")
            }
        }
        println("PASS: $assertions Android v13/v12 decode, C vectors, Fine UI and reconnect assertions")
    }
}
