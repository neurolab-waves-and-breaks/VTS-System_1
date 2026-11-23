package org.example.phaseviewer

/** Pure JVM tests of the actual v12 receiver-tracker and unchanged packet data. */
object RestartSyncChecks {
    private var tests = 0
    private fun checkCase(condition: Boolean, why: String) {
        tests++
        check(condition) { why }
    }
    private val base = VtsBleV12.decode(CoreChecks.hex("0C04050123003C000700080000000A00000085010000F691"))!!
    private fun p(seq: Int, phase: Int = 60, saved: Boolean = false) = base.copy(
        sequence = seq, phaseIndex = if (phase == -150) 12 else if (phase == 120) 2 else 5,
        phaseDeg = phase, rejectedMask = if (saved) 0x00F else 0,
        testedMask = if (saved) 0x020 else 0,
        permanentMask = if (saved) 0x010 else 0,
        flags = if (saved) 0x05 else 0x04
    )
    fun runAll() {
        tests = 0
        val t = VtsSnapshotTracker()
        checkCase(t.offer(p(230), "VTS", 1_000, 1_000), "Bind first snapshot")
        checkCase(t.isFresh(12_999), "One missed 5 s period tolerated")
        checkCase(!t.isFresh(13_000), "Lost/offline after 12 s, not instantly")
        checkCase(PhasePresentation.statusText(PhaseState(scanning=true, fresh=false, packet=t.packet)) ==
            PhasePresentation.WAITING, "Show signal lost, not a fabricated reset")
        // New device boot, old snapshot still fresh: first ad CANNOT replace it.
        t.reset();checkCase(t.offer(p(230), "VTS", 1_000, 1_000), "Rebind")
        checkCase(!t.offer(p(1, 120, true), "VTS", 1_900, 1_900), "First restart ad needs corroboration")
        checkCase(t.packet!!.sequence == 230 && t.packet!!.permanentMask == 0,
            "Uncorroborated packet must not overwrite the original masks")
        checkCase(!t.offer(p(1, 120, true), "VTS", 2_000, 2_000), "Duplicate candidate cannot confirm")
        checkCase(t.offer(p(2, 120, true), "VTS", 2_750, 2_750), "Two low increasing ads accepted before stale")
        checkCase(t.packet!!.permanentMask == 0x010 && t.packet!!.testedMask == 0x020 &&
            t.packet!!.rejectedMask == 0x00F, "Restored Permanent history remains receiver-authored")
        checkCase(t.isFresh(2_751), "Restart refreshes freshness")
        // Another scanner/address can never corroborate candidate.
        t.reset();t.offer(p(100), "A", 100, 100)
        checkCase(!t.offer(p(1), "A", 1_000, 1_000), "Restart candidate")
        checkCase(!t.offer(p(2), "B", 1_800, 1_800), "Other Bluetooth MAC denied")
        checkCase(t.packet!!.sequence == 100, "Still bound to A")
        checkCase(t.offer(p(2), "A", 1_850, 1_850), "Real bound MAC corroborates")
        // A single plausible lower sequence is insufficient at any age.
        t.reset();t.offer(p(10_000), "A", 100, 100)
        checkCase(!t.offer(p(9_999), "A", 200, 200), "Old/backwards sequence rejected")
        checkCase(!t.offer(p(9_998), "A", 300, 300), "More ambiguous backwards values rejected")
        checkCase(t.packet!!.sequence == 10_000, "No spurious rollback")
        checkCase(t.offer(p(10_001), "A", 400, 400), "Forward stream remains usable")
        // First pair missed: second initial ad + next 5 s heartbeat may corroborate.
        t.reset();t.offer(p(300), "A", 1_000, 1_000)
        checkCase(!t.offer(p(2), "A", 2_000, 2_000), "Second startup ad alone not enough")
        checkCase(t.offer(p(3), "A", 7_100, 7_100), "5 s follow-up corroborates")
        // Late timestamps, duplicated old seq, and long candidate gaps.
        t.reset();t.offer(p(250), "A", 1_000, 1_000)
        checkCase(!t.offer(p(1), "A", 700, 2_000), "Out-of-order Android timestamp refused")
        checkCase(!t.offer(p(1), "A", 2_000, 2_000), "Candidate pending")
        checkCase(!t.offer(p(2), "A", 10_200, 10_200), "Candidate older than 8 s must not confirm")
        checkCase(t.packet!!.sequence == 250, "Old state still retained until corroboration")
        checkCase(t.offer(p(3), "A", 10_900, 10_900), "New pair accepted")
        // Ordinary 16-bit sequence wrap must never look like reboot.
        t.reset();checkCase(t.offer(p(65535), "A", 100, 100), "Start at 65535")
        checkCase(t.offer(p(0), "A", 200, 200), "Wrap to zero")
        checkCase(t.offer(p(1), "A", 300, 300), "Wrap advance")
        // Very fast reboot from seq=2 also works after two early frames.
        t.reset();t.offer(p(2), "A", 100, 100)
        checkCase(!t.offer(p(1), "A", 200, 200), "Early restart candidate")
        checkCase(t.offer(p(2), "A", 1_050, 1_050), "Confirm same old seq with new low counter pair")
        // Stop scan clears stale/live history, not a fabricated new phase state.
        t.reset();checkCase(t.packet == null && t.address == null && !t.isFresh(2_000), "Stop clears")
        println("PASS: $tests Android restart/signal-loss policy assertions")
    }
}
