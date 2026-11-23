package org.example.phaseviewer

import kotlin.math.abs
import kotlin.random.Random

/** New UI geometry checks; inherited protocol tests are left unchanged. */
object DashboardChecks {
    fun runAll(): Int {
        var count = 0
        fun verify(condition: Boolean, message: String) { count++; check(condition) { message } }
        val random = Random(121)
        // Actual arrangement equation across compact/tall content and densities.
        repeat(2000) {
            val density = listOf(1f, 1.5f, 2f, 2.75f, 3f)[it % 5]
            val base = intArrayOf(48, 62, 54, 340, 94, 16)
            val sizes = base.map { h -> (h * density).toInt() }.toIntArray()
            val gap = (12 * density).toInt()
            val required = sizes.sum() + 5 * gap
            val total = required + random.nextInt(0, 1000)
            val pos = DashboardLayout.positions(total, sizes, gap)
            verify(pos.size == 6 && pos[0] == 0, "First section is not pushed down")
            verify(pos.last() + sizes.last() == total, "No unused footer well")
            for (i in 0..4) verify(pos[i + 1] >= pos[i] + sizes[i] + gap, "Minimum gap")
            val gaps = (0..4).map { j -> pos[j + 1] - pos[j] - sizes[j] }
            verify(gaps[1] + 2 >= gaps[4], "Space favours impedance/timer over footer")
            val short = DashboardLayout.positions(required - 1, sizes, gap)
            verify(short.last() + sizes.last() == required, "Small screen scrolls, does not squash")
        }
        verify(DashboardLayout.positions(100, intArrayOf(), 12).isEmpty(), "Empty input")
        verify(DashboardLayout.positions(100, intArrayOf(20), 12).contentEquals(intArrayOf(0)), "One section")
        verify(runCatching { DashboardLayout.positions(-1, intArrayOf(20), 12) }.isFailure, "Negative size rejected")
        verify(runCatching { DashboardLayout.positions(100, intArrayOf(-1), 12) }.isFailure, "Negative child rejected")
        // Both linear and nonlinear width curves; production measures using Compose.
        for (available in listOf(120f, 200f, 270f, 320f, 500f)) {
            for (natural in listOf(160f, 280f, 450f, 700f, 1300f)) {
                for (exponent in listOf(0.75, 1.0, 1.25)) {
                    fun measure(s: Float) = (natural * Math.pow(s.toDouble(), exponent)).toFloat()
                    val s = DashboardLayout.fitScale(available, ::measure)
                    verify(s > 0 && s <= 1f, "Positive font scaling")
                    verify(measure(s) <= available + 0.002f, "One-line width fits")
                    if (natural <= available) verify(s == 1f, "Ordinary sizes are not shrunk")
                    else verify(abs(measure(s) - available) < 0.01f, "Largest fitting typography")
                }
            }
        }
        verify(runCatching { DashboardLayout.fitScale(0f) { it } }.isFailure, "Zero width rejected")
        verify(runCatching { DashboardLayout.fitScale(Float.NaN) { it } }.isFailure, "NaN width rejected")
        verify(PhasePresentation.FOOTER == "v1.13a · dual-resolution viewer", "Revision footer")
        println("PASS: $count dashboard layout/typography assertions. Android rendering still needs phone inspection.")
        return count
    }
}
