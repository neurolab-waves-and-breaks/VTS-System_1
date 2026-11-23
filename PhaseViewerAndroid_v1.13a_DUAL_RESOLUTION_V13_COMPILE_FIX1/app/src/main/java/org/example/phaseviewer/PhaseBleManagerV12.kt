package org.example.phaseviewer

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log

/** Advertisement listener for protocols v12 and v13. Read-only; no CLIC/VTS commands. */
class PhaseBleManagerV12(private val app: Context, private val updateUi: (PhaseState) -> Unit) {
    private val main = Handler(Looper.getMainLooper())
    private val tracker = VtsSnapshotTracker()
    private var state = PhaseState()
    private var scanner: BluetoothLeScanner? = null
    private var callback: ScanCallback? = null
    private var generation = 0L
    private var scanStartedNs = 0L

    private fun publish(next: PhaseState) { state = next; updateUi(next) }
    private fun onMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) block() else main.post { block() }
    }
    private val freshnessCheck = object : Runnable {
        override fun run() {
            if (!state.scanning) return
            try {
                val adapter = (app.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
                if (!hasPermissions() || adapter?.isEnabled != true) {
                    stopInternal(); publish(PhaseState(status = "Turn on Bluetooth and allow Nearby devices")); return
                }
                val fresh = tracker.isFresh(SystemClock.elapsedRealtime())
                if (fresh != state.fresh) publish(state.copy(fresh = fresh))
            } catch (_: SecurityException) {
                stopInternal(); publish(PhaseState(status = "Bluetooth permission required")); return
            }
            main.postDelayed(this, 500L)
        }
    }
    fun reportPermissionDenied() = onMain {
        stopInternal(); publish(PhaseState(status = "Allow Bluetooth permission to view VTS"))
    }
    @SuppressLint("MissingPermission")
    fun start() = onMain {
        if (state.scanning) return@onMain
        stopInternal()
        if (!hasPermissions()) { publish(PhaseState(status = "Bluetooth permission required")); return@onMain }
        try {
            val adapter = (app.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
            if (adapter?.isEnabled != true) {
                publish(PhaseState(status = "Turn on Bluetooth, then tap Start scan")); return@onMain
            }
            val le = adapter.bluetoothLeScanner
            if (le == null) { publish(PhaseState(status = "Bluetooth scanner unavailable")); return@onMain }
            val token = generation
            val filters = listOf(VtsBleV12.VERSION, VtsBleV13.VERSION).map { version ->
                ScanFilter.Builder().setManufacturerData(
                    VtsBleDual.COMPANY_ID, byteArrayOf(version.toByte()), byteArrayOf(0xFF.toByte())
                ).build()
            }
            val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .setReportDelay(0L).build()
            val cb = object : ScanCallback() {
                override fun onScanResult(callbackType: Int, result: ScanResult) {
                    try {
                        val bytes = result.scanRecord?.getManufacturerSpecificData(VtsBleV12.COMPANY_ID) ?: return
                        val decoded = VtsBleDual.decode(bytes) ?: return
                        val from = result.device.address
                        val observedNs = result.timestampNanos
                        main.post {
                            if (token != generation || !state.scanning || observedNs < scanStartedNs) return@post
                            val now = SystemClock.elapsedRealtime()
                            if (tracker.offer(decoded, from, observedNs / 1_000_000L, now)) {
                                Log.i("VTS_VIEW", "RX v=${decoded.protocolVersion} ring=${if (decoded.fine) "fine" else "coarse"} parent=${decoded.fineParentIndex} seq=${decoded.sequence} phase=${decoded.phaseDeg} elapsed_s=${decoded.elapsedSeconds} " +
                                    "tested=${decoded.testedMask} rejected=${decoded.rejectedMask} permanent=${decoded.permanentMask}")
                                publish(state.copy(packet = decoded, fresh = tracker.isFresh(now),
                                    receivedMonotonicMs = observedNs / 1_000_000L))
                            }
                        }
                    } catch (_: SecurityException) {
                        main.post {
                            if (token == generation) { stopInternal(); publish(PhaseState(status = "Bluetooth permission required")) }
                        }
                    }
                }
                override fun onBatchScanResults(results: MutableList<ScanResult>) {
                    results.sortedBy { it.timestampNanos }.forEach { onScanResult(ScanSettings.CALLBACK_TYPE_ALL_MATCHES, it) }
                }
                override fun onScanFailed(errorCode: Int) {
                    main.post {
                        if (token != generation) return@post
                        Log.w("VTS_VIEW", "Android scan error $errorCode")
                        stopInternal(); publish(PhaseState(status = "Scan stopped — tap Start scan to retry"))
                    }
                }
            }
            scanner = le; callback = cb
            scanStartedNs = SystemClock.elapsedRealtimeNanos()
            publish(PhaseState(scanning = true, status = "Searching for VTS"))
            le.startScan(filters, settings, cb)
            main.postDelayed(freshnessCheck, 500L)
        } catch (_: SecurityException) {
            stopInternal(); publish(PhaseState(status = "Bluetooth permission required"))
        } catch (error: Exception) {
            Log.w("VTS_VIEW", "Scanner start failed: ${error.javaClass.simpleName}")
            stopInternal(); publish(PhaseState(status = "Unable to scan — check Bluetooth"))
        }
    }
    private fun hasPermissions(): Boolean {
        val required = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        return required.all { app.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }
    }
    @SuppressLint("MissingPermission")
    private fun stopInternal() {
        generation++ // Ignore queued callbacks from a stopped/previous scan.
        main.removeCallbacks(freshnessCheck)
        try { callback?.let { scanner?.stopScan(it) } } catch (_: Exception) { }
        scanner = null; callback = null; scanStartedNs = 0L; tracker.reset()
    }
    /** Stops the PHONE scanner, not the VTS device or stimulation. */
    fun stop() = onMain { stopInternal(); publish(PhaseState(status = "Not scanning")) }
}
