package org.example.phaseviewer

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import kotlinx.coroutines.delay
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

class MainActivity : ComponentActivity() {
    private var viewState by mutableStateOf(PhaseState())
    private var foreground = false
    private var requestingPermission = false
    private val manager by lazy { PhaseBleManagerV12(applicationContext) { viewState = it } }
    private val permissionLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        requestingPermission = false
        if (foreground) {
            if (allPermissionsGranted()) manager.start() else manager.reportPermissionDenied()
        }
    }
    private fun neededPermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    private fun allPermissionsGranted() = neededPermissions().all {
        checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
    }
    private fun startScan() {
        if (allPermissionsGranted()) manager.start()
        else if (!requestingPermission) { requestingPermission = true; permissionLauncher.launch(neededPermissions()) }
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.BLACK),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.BLACK)
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(background = BG, surface = BG, primary = CURRENT)) {
                PhaseViewerScreen(viewState, onStart = { startScan() }, onStop = { manager.stop() })
            }
        }
    }
    override fun onStart() { super.onStart(); foreground = true }
    override fun onStop() {
        foreground = false
        manager.stop() // No background scan or fabricated continuation of live values.
        super.onStop()
    }
}

private val BG = Color(0xFF000000)
private val CURRENT = Color(0xFF5CE2A2)
private val PERMANENT = Color(0xFFF9C65D)
private val UNTESTED = Color(0xFF8C939C)
private val TESTED = Color(0xFF65A8E0)
private val SECONDARY = Color(0xFFBDC4CC)
private fun phaseColor(role: PhaseRole): Color = when (role) {
    PhaseRole.REMOVED -> BG
    PhaseRole.UNTESTED -> UNTESTED
    PhaseRole.TESTED -> TESTED
    PhaseRole.CURRENT -> CURRENT
    PhaseRole.PERMANENT -> PERMANENT
}

// Presentation-only surfaces: impedance is never coloured as a contact-quality score.
private val PANEL = Color(0xFF101316)
private val OUTLINE = Color(0xFF2A3036)
private val PANEL_SHAPE = RoundedCornerShape(20.dp)

/** Distribute spare screen height between sections, not into an empty footer well. */
private object DashboardArrangement : Arrangement.Vertical {
    override val spacing: Dp = 12.dp
    override fun Density.arrange(totalSize: Int, sizes: IntArray, outPositions: IntArray) {
        val positions = DashboardLayout.positions(totalSize, sizes, spacing.roundToPx())
        positions.copyInto(outPositions)
    }
}

@Composable
private fun PhaseViewerScreen(state: PhaseState, onStart: () -> Unit, onStop: () -> Unit) {
    val packet = state.packet
    val dimmed = packet != null && !state.fresh
    BoxWithConstraints(Modifier.fillMaxSize().background(BG).safeDrawingPadding()) {
        // Read finite viewport constraints BEFORE adding scrolling. A tall screen
        // distributes its free space; a short/large-font screen scrolls naturally.
        val viewportHeight = maxHeight
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
            Column(
                Modifier.fillMaxWidth().heightIn(min = viewportHeight)
                    .padding(horizontal = 20.dp, vertical = 16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = DashboardArrangement
            ) {
                ScanHeader(state, onStart, onStop, Modifier.widthIn(max = 520.dp).fillMaxWidth())
                ImpedanceRow(state, Modifier.widthIn(max = 520.dp).fillMaxWidth())
                // Bounded presentation-only interpolation from the last confirmed receiver time.
                // Never changes VTS timer, phase state, Bluetooth scan or stimulation.
                ReceiverSynchronizedTimer(state, dimmed)
                BoxWithConstraints(Modifier.widthIn(max = 520.dp).fillMaxWidth()) {
                    // Capture the BoxWithConstraints scope before entering ColumnScope.
                    val dialMaxWidth = maxWidth
                    val extraHeight = (LocalDensity.current.fontScale - 1f).coerceAtLeast(0f) * 120f
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text(PhasePresentation.ringTitle(packet), color = SECONDARY, fontSize = 14.sp,
                            lineHeight = 19.sp, textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth().semantics { contentDescription = PhasePresentation.ringTitle(packet) })
                        PhaseDial(packet, dimmed, Modifier.fillMaxWidth().height(minOf(dialMaxWidth, 320.dp) + extraHeight.dp))
                        PhasePresentation.selectedPhaseNote(packet)?.let { note ->
                            Text(note, color = SECONDARY.copy(alpha = if (dimmed) 0.55f else 1f),
                                fontSize = 12.sp, lineHeight = 16.sp, textAlign = TextAlign.Center)
                        }
                    }
                }
                LegendPanel(Modifier.widthIn(max = 520.dp).fillMaxWidth())
                Text(PhasePresentation.FOOTER, color = SECONDARY, fontSize = 12.sp,
                    lineHeight = 16.sp, textAlign = TextAlign.Center)
            }
        }
    }
}


/**
 * Refreshes only this phone's number once a second. The underlying receiver
 * snapshot is never modified; no timer/phase events are sent to VTS.
 */
@Composable
private fun ReceiverSynchronizedTimer(state: PhaseState, dimmed: Boolean) {
    var nowMs by remember(state.scanning, state.packet?.sequence) {
        mutableLongStateOf(SystemClock.elapsedRealtime())
    }
    LaunchedEffect(state.scanning, state.packet?.sequence, state.fresh) {
        if (state.scanning && state.fresh && state.packet != null) {
            // ~4 Hz refresh allows accurate one-second labels without a 1 s
            // boundary delay. Stops on scan close, lifecycle stop or disconnection.
            while (true) {
                nowMs = SystemClock.elapsedRealtime()
                delay(250L)
            }
        }
    }
    val clock = PhoneTimer.present(state, nowMs)
    val label = clock.seconds?.let(VtsBleV12::timerLabel) ?: "—:—"
    Text(
        label,
        color = Color.White.copy(alpha = if (dimmed) 0.45f else 1f),
        fontSize = 44.sp, lineHeight = 54.sp, fontWeight = FontWeight.Medium,
        style = TextStyle(fontFeatureSettings = "tnum"),
        textAlign = TextAlign.Center,
        modifier = Modifier.semantics {
            contentDescription = when {
                dimmed -> "Last estimated phase time $label; waiting for update"
                clock.estimated -> "Estimated phase time $label; synchronised to receiver"
                else -> "Received phase time $label"
            }
        }
    )
}
/** Same start/stop callbacks and enabled states; only the currently usable action is shown. */
@Composable
private fun ScanHeader(state: PhaseState, onStart: () -> Unit, onStop: () -> Unit, modifier: Modifier) {
    BoxWithConstraints(modifier) {
        val largeText = LocalDensity.current.fontScale > 1.35f
        val label = if (state.scanning) "Stop scan" else "Start scan"
        val callback = if (state.scanning) onStop else onStart
        val status = PhasePresentation.statusText(state)
        if (largeText || maxWidth < 300.dp) {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(12.dp)) {
                ScanStatus(status, Modifier.fillMaxWidth())
                ScanAction(label, state.scanning, callback, Modifier.fillMaxWidth())
            }
        } else {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                ScanStatus(status, Modifier.weight(1f))
                ScanAction(label, state.scanning, callback, Modifier)
            }
        }
    }
}

@Composable
private fun ScanStatus(status: String, modifier: Modifier) {
    Row(modifier.semantics(mergeDescendants = true) {}, verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        // Neutral dot: it is not a stimulation or electrode-quality indicator.
        Canvas(Modifier.size(6.dp)) { drawCircle(SECONDARY) }
        Text(status, color = SECONDARY, fontSize = 15.sp, lineHeight = 21.sp)
    }
}

@Composable
private fun ScanAction(label: String, scanning: Boolean, callback: () -> Unit, modifier: Modifier) {
    val buttonModifier = modifier.heightIn(min = 48.dp)
    if (scanning) {
        OutlinedButton(onClick = callback, modifier = buttonModifier, shape = RoundedCornerShape(14.dp),
            border = androidx.compose.foundation.BorderStroke(1.dp, OUTLINE),
            colors = ButtonDefaults.outlinedButtonColors(containerColor = PANEL, contentColor = Color.White),
            contentPadding = PaddingValues(horizontal = 18.dp, vertical = 12.dp)) {
            Text(label, fontSize = 16.sp, fontWeight = FontWeight.Medium)
        }
    } else {
        Button(onClick = callback, modifier = buttonModifier, shape = RoundedCornerShape(14.dp),
            colors = ButtonDefaults.buttonColors(containerColor = Color.White, contentColor = BG),
            contentPadding = PaddingValues(horizontal = 18.dp, vertical = 12.dp)) {
            Text(label, fontSize = 16.sp, fontWeight = FontWeight.Medium)
        }
    }
}

/** One baseline, with full numeric value and unit; never wrap or ellipsise a reading. */
@Composable
private fun ImpedanceRow(state: PhaseState, modifier: Modifier) {
    val value = PhasePresentation.impedanceText(state)
    val title = PhasePresentation.IMPEDANCE_TITLE
    val measurer = rememberTextMeasurer()
    BoxWithConstraints(
        modifier.background(PANEL, PANEL_SHAPE).border(1.dp, OUTLINE, PANEL_SHAPE)
            .padding(horizontal = 18.dp, vertical = 14.dp)
    ) {
        val density = LocalDensity.current
        val availablePx = with(density) { maxWidth.toPx() }
        val gapPx = with(density) { 12.dp.toPx() }
        fun titleStyle(scale: Float) = TextStyle(fontSize = (17f * scale).sp, lineHeight = (22f * scale).sp,
            fontFamily = FontFamily.SansSerif, letterSpacing = 0.sp, fontWeight = FontWeight.Normal)
        fun valueStyle(scale: Float) = TextStyle(fontSize = (26f * scale).sp, lineHeight = (34f * scale).sp,
            fontFamily = FontFamily.SansSerif, letterSpacing = 0.sp, fontWeight = FontWeight.SemiBold,
            fontFeatureSettings = "tnum")
        // Measure with Compose's real text measurement (including system scaling),
        // not a character-count estimate. Re-evaluate for an updated digit count.
        val scale = remember(title, value, maxWidth, density.density, density.fontScale, measurer) {
            DashboardLayout.fitScale((availablePx - gapPx - 2f).coerceAtLeast(1f)) { candidate ->
                val left = measurer.measure(title, titleStyle(candidate), softWrap = false,
                    maxLines = 1, constraints = Constraints()).size.width
                val right = measurer.measure(value, valueStyle(candidate), softWrap = false,
                    maxLines = 1, constraints = Constraints()).size.width
                (left + right).toFloat()
            }
        }
        Row(Modifier.fillMaxWidth().semantics(mergeDescendants = true) {},
            verticalAlignment = Alignment.CenterVertically) {
            Text(title, color = SECONDARY, style = titleStyle(scale), softWrap = false, maxLines = 1,
                modifier = Modifier.alignByBaseline())
            Spacer(Modifier.width(12.dp))
            Spacer(Modifier.weight(1f))
            Text(value, color = Color.White, style = valueStyle(scale), softWrap = false, maxLines = 1,
                modifier = Modifier.alignByBaseline())
        }
    }
}

private val LEGEND_STYLE = TextStyle(fontFamily = FontFamily.SansSerif, fontSize = 18.sp,
    lineHeight = 24.sp, letterSpacing = 0.sp, fontWeight = FontWeight.Medium)

@Composable
private fun LegendPanel(modifier: Modifier) {
    val measurer = rememberTextMeasurer()
    BoxWithConstraints(modifier.background(PANEL, PANEL_SHAPE).border(1.dp, OUTLINE, PANEL_SHAPE)
        .padding(horizontal = 16.dp, vertical = 16.dp)) {
        val density = LocalDensity.current
        val widest = PhasePresentation.LEGEND.maxOf { (_, label) ->
            measurer.measure(label, LEGEND_STYLE, softWrap = false, maxLines = 1,
                constraints = Constraints()).size.width
        }
        val cellWidth = widest + with(density) { 21.dp.toPx() }
        val twoColumns = 2 * cellWidth + with(density) { 12.dp.toPx() } <= with(density) { maxWidth.toPx() }
        Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            // Two rows normally. Larger accessibility fonts use four full-width
            // rows instead of clipping, breaking or shrinking the colour labels.
            if (twoColumns) {
                PhasePresentation.LEGEND.chunked(2).forEach { row ->
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        row.forEach { (role, label) -> LegendDot(phaseColor(role), label, Modifier.weight(1f)) }
                    }
                }
            } else {
                PhasePresentation.LEGEND.forEach { (role, label) ->
                    LegendDot(phaseColor(role), label, Modifier.fillMaxWidth())
                }
            }
        }
    }
}

@Composable
private fun LegendDot(color: Color, label: String, modifier: Modifier) {
    Row(modifier.semantics(mergeDescendants = true) {}, verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(9.dp)) {
        Canvas(Modifier.size(12.dp)) { drawCircle(color) }
        Text(label, color = Color.White, style = LEGEND_STYLE)
    }
}

@Composable
private fun PhaseDial(packet: VtsBleV12Packet?, dimmed: Boolean, modifier: Modifier) {
    val description = buildString {
        append(if (dimmed) "Last received phase " else "Phase ")
        append(PhasePresentation.phaseText(packet))
        if (packet?.permanent == true) append(", Permanent")
        append(". Available phases: ")
        (packet?.ringPhases ?: VtsBleV12.PHASES).forEachIndexed { i, degree ->
            if (PhasePresentation.role(packet, i) != PhaseRole.REMOVED) {
                append("${VtsBleV12.phaseLabel(degree)} ${PhasePresentation.role(packet, i).name.lowercase()}; ")
            }
        }
    }
    Canvas(modifier.semantics { contentDescription = description }) {
        val opacity = if (dimmed) 0.45f else 1f
        val labelPaint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.White.copy(alpha = opacity).toArgb()
            // Fit the angular labels on narrow/large-font displays; other
            // texts follow system font scaling and the page can scroll.
            textSize = minOf(18.sp.toPx(), size.width * 0.065f)
            typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            textAlign = android.graphics.Paint.Align.CENTER
        }
        val phases = packet?.ringPhases ?: VtsBleV12.PHASES
        val widest = phases.maxOf { labelPaint.measureText(VtsBleV12.phaseLabel(it)) }
        val labelHeight = labelPaint.fontMetrics.descent - labelPaint.fontMetrics.ascent
        val layout = if (packet?.fine == true) DialGeometry.layoutFine(size.width, size.height, density, widest, labelHeight)
                     else DialGeometry.layout(size.width, size.height, density, widest, labelHeight)
        val center = Offset(layout.cx, layout.cy)
        drawCircle(Color(0xFF343A40).copy(alpha = opacity), layout.radius, center, style = Stroke(1.5.dp.toPx()))
        // No cross, spokes, title or second selected-phase card.
        phases.forEachIndexed { i, _ ->
            val role = PhasePresentation.role(packet, i)
            val p = layout.positions[i]
            val point = Offset(p.x, p.y)
            if (role == PhaseRole.REMOVED) {
                // True black, also erasing the guide beneath this marker. No text label.
                drawCircle(BG, 12.dp.toPx(), point)
            } else {
                val current = packet?.phaseIndex == i + 1
                val color = phaseColor(role).copy(alpha = opacity)
                if (current) drawCircle(color.copy(alpha = 0.18f * opacity), 17.dp.toPx(), point)
                drawCircle(color, if (current) 9.dp.toPx() else 7.dp.toPx(), point)
                PhasePresentation.rimLabel(packet, i)?.let { label ->
                    drawIntoCanvas { canvas ->
                        canvas.nativeCanvas.drawText(label, p.labelX,
                            p.labelY - (labelPaint.fontMetrics.ascent + labelPaint.fontMetrics.descent) / 2f, labelPaint)
                    }
                }
            }
        }
        val text = PhasePresentation.phaseText(packet)
        val centrePaint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
            color = (if (packet?.permanent == true) PERMANENT else if (packet == null || packet.startup) Color.White else CURRENT)
                .copy(alpha = opacity).toArgb()
            textSize = 44.sp.toPx()
            typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            textAlign = android.graphics.Paint.Align.CENTER
        }
        val availableWidth = layout.radius * 1.55f
        val measured = centrePaint.measureText(text)
        if (measured > availableWidth && measured > 0f) centrePaint.textSize *= availableWidth / measured
        drawIntoCanvas { canvas ->
            canvas.nativeCanvas.drawText(text, center.x,
                center.y - (centrePaint.fontMetrics.ascent + centrePaint.fontMetrics.descent) / 2f, centrePaint)
        }
    }
}

/** Design previews only; not reachable from the installed app and never injected into scanning. */
@Preview(name = "Supervisor layout — Tested", widthDp = 390, heightDp = 844, showBackground = true, backgroundColor = 0xFF000000)
@Composable
private fun TestedPreview() {
    val p = VtsBleV12.decode(hexForPreview("0C04050123003C000700080000000A00000085010000F691"))
    MaterialTheme(colorScheme = darkColorScheme()) { PhaseViewerScreen(PhaseState(true, true, packet = p), {}, {}) }
}
@Preview(name = "Supervisor layout — Permanent", widthDp = 390, heightDp = 844, showBackground = true, backgroundColor = 0xFF000000)
@Composable
private fun PermanentPreview() {
    val p = VtsBleV12.decode(hexForPreview("0C0504011E00B40007000000080009000000A0010000038C"))
    MaterialTheme(colorScheme = darkColorScheme()) { PhaseViewerScreen(PhaseState(true, true, packet = p), {}, {}) }
}
private fun hexForPreview(hex: String) = hex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
