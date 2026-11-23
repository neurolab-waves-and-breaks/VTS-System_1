package org.example.phaseviewer

object PhoneTimerChecks {
    private var assertions = 0
    private fun verify(ok: Boolean, message: String) { assertions++; check(ok) { message } }
    fun runAll(): Int {
        assertions = 0
        val base = VtsBleV12.decode(CoreChecks.hex(CoreChecks.realHex[0]))!! // active phase -60, time 1:41
        val state = PhaseState(scanning = true, fresh = true, packet = base, receivedMonotonicMs = 1000)
        fun time(s: PhaseState, now: Long) = PhoneTimer.present(s, now).seconds
        verify(time(state, 1000) == 101L, "Raw sample unchanged at receive")
        verify(time(state, 1999) == 101L, "Not early")
        verify(time(state, 2000) == 102L, "One second later")
        verify(time(state, 6000) == 106L, "Five-second advertising gap bridged")
        verify(time(state, 7999) == 107L, "Six seconds")
        verify(time(state, 8000) == 108L, "Seven-second maximum")
        verify(time(state, 15000) == 108L, "Missed BLE frames freeze rather than invent time")
        verify(time(state.copy(fresh=false), 150000) == 108L, "Stale remains frozen and bounded")
        verify(!PhoneTimer.present(state,1000).estimated && PhoneTimer.present(state,2000).estimated,
            "Estimate is identifiable")
        verify(time(state.copy(receivedMonotonicMs=null), 5000) == 101L, "No anchor means no extrapolation")
        verify(time(state, 999) == 101L, "Clock inversion means no extrapolation")
        verify(time(state.copy(scanning=false), 5000) == 101L, "Stop scan cannot keep ticking")
        verify(time(state.copy(packet=base.copy(flags=0)), 5000) == 101L,
            "Inactive device holds the actual receiver value")
        verify(time(state.copy(packet=base.copy(flags=base.flags or 2)), 5000) == 101L,
            "Pending phase transaction never extrapolated")
        verify(time(state.copy(packet=base.copy(flags=base.flags or 8)), 5000) == 101L,
            "Conflict never extrapolated")
        verify(time(state.copy(packet=base.copy(flags=base.flags or 16)), 5000) == 101L,
            "Fault never extrapolated")
        verify(time(state.copy(packet=null),5000) == null, "No values made up without a packet")
        verify(time(state.copy(packet=base.copy(flags=32)), 5000) == null, "Startup is unknown")
        val newest = VtsBleV12.decode(CoreChecks.hex(CoreChecks.realHex[1]))!! // +120, time 0:00
        verify(time(state.copy(packet=newest,receivedMonotonicMs=9000),9000) == 0L,
            "Resync to confirmed 0:00 after a phase change")
        verify(time(state.copy(packet=newest,receivedMonotonicMs=9000),10000) == 1L,
            "New phase clock increases smoothly")
        val missed = VtsBleV12.decode(CoreChecks.hex(CoreChecks.realHex[8]))!! // 0:10
        verify(time(state.copy(packet=missed,receivedMonotonicMs=25000),25000) == 10L,
            "Do not fake an unseen 0:00 after BLE loss")
        val permanent = VtsBleV12.decode(CoreChecks.hex(CoreChecks.realHex[6]))!!
        verify(time(state.copy(packet=permanent,receivedMonotonicMs=30000),31000) == 10L,
            "Permanent mode still allows the receiver timer to progress")
        val high = base.copy(elapsedSeconds=VtsBleV12.UNAVAILABLE-2L)
        verify(time(state.copy(packet=high),15000) == VtsBleV12.UNAVAILABLE,
            "Overflow is saturated to the receiver unsigned timer range")
        for (age in 0L..60000L step 17L) {
            val value = time(state, 1000+age)!!
            verify(value in 101L..108L, "Bounded even across long BLE loss age=$age")
            if (age > PhoneTimer.MAX_ESTIMATE_MS) verify(value == 108L, "Long gap freezes age=$age")
        }
        return assertions
    }
}
