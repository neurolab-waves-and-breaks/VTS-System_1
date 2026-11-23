package org.example.phaseviewer

import kotlin.math.roundToInt

/** Pure presentation geometry. Has no packet, Bluetooth, timer or receiver state. */
object DashboardLayout {
    // Header→impedance, impedance→timer, timer→dial, dial→legend, legend→footer.
    private val gapWeights = floatArrayOf(1.05f, 1.55f, 1.25f, 1f, 0.55f)

    fun positions(totalSize: Int, childSizes: IntArray, minimumGap: Int): IntArray {
        require(totalSize >= 0 && minimumGap >= 0 && childSizes.all { it >= 0 })
        if (childSizes.isEmpty()) return intArrayOf()
        val gapCount = childSizes.size - 1
        if (gapCount == 0) return intArrayOf(0)
        val required = childSizes.sumOf { it.toLong() } + gapCount.toLong() * minimumGap
        val spare = (totalSize.toLong() - required).coerceAtLeast(0).toFloat()
        val weights = if (childSizes.size == 6) gapWeights else FloatArray(gapCount) { 1f }
        val totalWeight = weights.sum()
        var consumed = 0L
        var cumulativeWeight = 0f
        val result = IntArray(childSizes.size)
        for (i in childSizes.indices) {
            val extra = if (i == gapCount) spare.roundToInt() else (spare * cumulativeWeight / totalWeight).roundToInt()
            result[i] = (consumed + extra).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            consumed += childSizes[i].toLong() + minimumGap
            if (i < gapCount) cumulativeWeight += weights[i]
        }
        return result
    }

    /** Largest scale ≤1 fitting a monotonic, actual text-width measurement. */
    fun fitScale(availableWidth: Float, measureWidth: (Float) -> Float): Float {
        require(availableWidth.isFinite() && availableWidth > 0f)
        if (measureWidth(1f) <= availableWidth) return 1f
        var low = 0.001f
        var high = 1f
        repeat(20) {
            val mid = (low + high) / 2f
            if (measureWidth(mid) <= availableWidth) low = mid else high = mid
        }
        return low
    }
}
