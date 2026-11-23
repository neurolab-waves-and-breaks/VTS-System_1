package org.example.phaseviewer
import org.junit.Test
import org.junit.Assert.assertTrue
class PhoneTimerTest { @Test fun boundedReceiverSynchronizedTimer() { assertTrue(PhoneTimerChecks.runAll() > 6000) } }
