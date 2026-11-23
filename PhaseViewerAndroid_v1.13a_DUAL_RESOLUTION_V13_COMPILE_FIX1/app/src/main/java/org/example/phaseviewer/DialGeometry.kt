package org.example.phaseviewer

import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/** Pixel geometry shared by the actual Compose dial and host bounds tests. */
data class DialPosition(val x: Float, val y: Float, val labelX: Float, val labelY: Float)
data class DialLayout(val cx: Float, val cy: Float, val radius: Float, val positions: List<DialPosition>)
object DialGeometry {
    fun layout(width: Float, height: Float, density: Float, maxLabelWidth: Float, labelHeight: Float): DialLayout {
        require(width > 0 && height > 0 && density > 0 && maxLabelWidth >= 0 && labelHeight >= 0)
        val cx = width / 2f; val cy = height / 2f
        val labelRadius = (width / 2f - maxLabelWidth / 2f - 8f * density).coerceAtLeast(1f)
        val labelRadiusY = (height / 2f - labelHeight / 2f - 8f * density).coerceAtLeast(1f)
        // Reserve the complete label half-width/height plus a node gap, not
        // merely a fixed radial offset that can make +/-90 labels touch nodes.
        val radius = min(labelRadius - maxLabelWidth / 2f - 12f * density,
                         labelRadiusY - labelHeight / 2f - 12f * density).coerceAtLeast(1f)
        val points = VtsBleV12.PHASES.map { deg ->
            val theta = Math.toRadians(deg.toDouble())
            val dx = sin(theta).toFloat(); val dy = -cos(theta).toFloat()
            var ly = cy + labelRadiusY * dy
            // Separate the near-cardinal labels vertically at enlarged system fonts.
            if (deg == -30 || deg == 30) ly = maxOf(ly, cy - labelRadiusY + labelHeight + 3f * density)
            if (deg == -150 || deg == 150) ly = minOf(ly, cy + labelRadiusY - labelHeight - 3f * density)
            if (deg == -60 || deg == 60) ly = maxOf(ly, cy - labelRadiusY + 2f * (labelHeight + 3f * density))
            if (deg == -120 || deg == 120) ly = minOf(ly, cy + labelRadiusY - 2f * (labelHeight + 3f * density))
            var lx = cx + labelRadius * dx
            if (kotlin.math.abs(deg) == 30 || kotlin.math.abs(deg) == 150) {
                val separation = maxOf(kotlin.math.abs(lx - cx), maxLabelWidth / 2f + 2f * density)
                lx = cx + (if (dx < 0) -separation else separation)
            }
            DialPosition(cx + radius * dx, cy + radius * dy, lx, ly)
        }
        return DialLayout(cx, cy, radius, points)
    }

    /** Seven nearby angles must be spaced by MEMBER ORDER, not physical degrees.
     * Otherwise 75..105 degrees would overlap in a tiny sector of the dial.
     */
    fun layoutFine(width: Float, height: Float, density: Float, maxLabelWidth: Float, labelHeight: Float): DialLayout {
        require(width > 0 && height > 0 && density > 0 && maxLabelWidth >= 0 && labelHeight >= 0)
        val cx = width / 2f; val cy = height / 2f
        val lxRadius = (cx - maxLabelWidth / 2f - 8f * density).coerceAtLeast(1f)
        val lyRadius = (cy - labelHeight / 2f - 8f * density).coerceAtLeast(1f)
        val radius = min(lxRadius - maxLabelWidth / 2f - 12f * density,
                         lyRadius - labelHeight / 2f - 12f * density).coerceAtLeast(1f)
        val positions = (0 until 7).map { member ->
            val theta = 2.0 * Math.PI * member / 7.0
            val dx = sin(theta).toFloat(); val dy = -cos(theta).toFloat()
            DialPosition(cx + radius * dx, cy + radius * dy, cx + lxRadius * dx, cy + lyRadius * dy)
        }
        return DialLayout(cx, cy, radius, positions)
    }
}
