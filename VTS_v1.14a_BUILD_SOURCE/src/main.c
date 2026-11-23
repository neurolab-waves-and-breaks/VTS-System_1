#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <nrfx.h>

#include <esb.h>
#include "clic.h"
#include "clic_reply.h"
#include "phase_selection.h"
#include "phase_wire.h"
#include "recovery_policy.h"
#include "reconnect_policy.h"
#include "ble_telemetry.h"
#include "vts_phase_runtime.h"
#include "vts_stage2_config.h"

#if defined(NRF54L15_XXAA)
#include <hal/nrf_clock.h>
#endif

#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif

LOG_MODULE_REGISTER(esb_prx_da7280, LOG_LEVEL_INF);

/* v1.3 diagnostics: USB console is independent of CLIC's USB dongle.
 * LOG_INF uses Zephyr's deferred UART backend; do not log EEG samples
 * at 256 Hz or print from GPIO ISR.
 */
#define VTS_DIAG_USB_NODE DT_CHOSEN(zephyr_console)
BUILD_ASSERT(DT_NODE_EXISTS(VTS_DIAG_USB_NODE), "Missing USB CDC ACM console");
static const struct device *const vts_diag_uart = DEVICE_DT_GET(VTS_DIAG_USB_NODE);
static bool vts_diag_port_open;
static atomic_t vts_diag_rx_ping;
static atomic_t vts_diag_rx_eeg;
static atomic_t vts_diag_rx_response;
static atomic_t vts_diag_rx_stim;



/* Huru packet format: [type:1][body_length:1][body:body_length].
 * VTS commands use body[0] = legacy subcommand (1=config, 2=sleep).
 * Do not auto-detect the old protocol: its IDs collide with packet types.
 */
#define VTS_SUBCMD_CHANGE_HAPTIC  0x01U
#define VTS_SUBCMD_CHANGE_SLEEP   0x02U

#define BUTTON_NODE DT_ALIAS(vts_phase_button)
BUILD_ASSERT(DT_NODE_EXISTS(BUTTON_NODE), "Missing vts-phase-button alias");
static const struct gpio_dt_spec phase_button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
#define BUTTON_DEBOUNCE_MS 25U
#define BUTTON_LONG_PRESS_MS 1000U
#define REJECTION_BUZZ_MS 500U

/* CLIC opcode 30 uses signed big-endian int32 degrees (Rob's clarification,
 * October 2026). Real bridge/CLIC interoperability still needs testing.
 */
static struct nvs_fs fs;

#define NVS_PARTITION            storage_partition
#define NVS_PARTITION_DEVICE     FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET     FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define KEY_ID                   1
#define BUILD_ID_KEY_ID          2
/* Separate, previously unused NVS ID. Never change IDs 1/2 or the haptic
 * build compatibility signature. */
#define VTS_PERMANENT_KEY_ID     0x5301
/* Fixed NVS compatibility ID: changing the test mode does not change NVS
 * identity or automatically reset saved stimulation settings.
 */
/* Keep v1.0 NVS compatibility ID: do not reset saved intensity/duration
 * for a wire-protocol-only update. This is NOT the displayed release version.
 */
#define FIRMWARE_BUILD_ID        "DA7280_CLIC_PHASE_V1.0"
#define FIRMWARE_RELEASE_ID      "VTS_V1.14A_DUAL_RESOLUTION_FSM_EXPERIMENTAL"

static const char firmware_build_id[] = FIRMWARE_BUILD_ID;

/*
 * NVS layout is kept at 8 bytes.
 * key[0] = intensity (%), key[2] = duration (ms), key[7] = mirrored duration.
 */
static uint8_t key[8];
#define KEY_PULSE_ON_MS_INDEX    7

/* DA7280 over I2C */

#define I2C_BUS_NODE DT_NODELABEL(i2c1)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_BUS_NODE);

#define DA7280_ADDR  0x4A

/* DA7280 registers */
#define REG_IRQ_EVENT1        0x03
/* v1.13d read-only haptic diagnosis: DA7280 register map, Renesas datasheet.
 * These are NEVER written by diagnostics; the original recovery routine
 * remains the only owner of IRQ_EVENT1 write-one-to-clear behaviour. */
#define REG_IRQ_WARNING_DIAG  0x04
#define REG_IRQ_SEQ_DIAG      0x05
#define REG_IRQ_STATUS1       0x06
#define REG_IRQ_ACTUATOR_DIAG 0x81
#define REG_CIF_I2C1          0x08
#define REG_FRQ_LRA_PER_H     0x0A
#define REG_FRQ_LRA_PER_L     0x0B
#define REG_ACTUATOR_NOMMAX   0x0C
#define REG_ACTUATOR_ABSMAX   0x0D
#define REG_ACTUATOR_IMAX     0x0E
#define REG_CALIB_V2I_H       0x0F
#define REG_CALIB_V2I_L       0x10
#define REG_TOP_CFG1          0x13
#define REG_TOP_CFG2          0x14
#define REG_TOP_CFG4          0x16
#define REG_TOP_INT_CFG1      0x17
#define REG_TOP_CTL1          0x22
#define REG_TOP_CTL2          0x23
#define REG_SEQ_CTL1          0x24
#define REG_SEQ_CTL2          0x28
#define REG_GPI_0_CTL         0x29
#define REG_MEM_CTL2          0x2D
#define REG_TRIM3             0x5F
#define REG_TRIM6             0x62
#define REG_TOP_CFG5          0x6E
#define REG_SNP_MEM_0         0x84

/* ETWM playback control */
#define DA7280_MODE_INACTIVE          0x00
#define DA7280_MODE_ETWM              0x04
#define DA7280_STANDBY_EN             0x08
#define DA7280_SEQ_START              0x10
#define DA7280_TOP_CTL1_ETWM_STANDBY  (DA7280_STANDBY_EN | DA7280_MODE_ETWM)
#define DA7280_TOP_CTL1_ETWM_START    (DA7280_SEQ_START | DA7280_TOP_CTL1_ETWM_STANDBY)
#define DA7280_IRQ_SEQ_DONE            BIT(2)

#define DA7280_ETWM_MEM_SIZE          100U
#define DA7280_ETWM_TIMEBASE_US       1360U

/* Validated profile: tracking, acceleration and rapid stop disabled.
 * BEMF sensing and the normal fault limit remain enabled.
 */

/* TRIM6 setting for 25 mV/ns H-bridge edge rate. */
#define DA7280_TRIM6_ERC25_VALUE 0x60

/*
 * Fixed nominal drive frequency: ~170 Hz.
 * Tracking may adjust this only when enabled.
 */
#define DA7280_LRA_PER_170HZ_H 0x22
#define DA7280_LRA_PER_170HZ_L 0x3C

/* Validated actuator voltage limit; ABSMAX scales amplitude within 0x00..0x6B. */
#define DA7280_ACTUATOR_VMAX_REG 0x6B

/* Keep V2I fixed at 0x00D1; BEMF sensing/fault protection remain enabled. */
#define DA7280_TOP_CFG4_FREEZE_IMPEDANCE       0xC0U
#define DA7280_TOP_INT_CFG1_NORMAL_BEMF_FAULT  0x81U

BUILD_ASSERT((DA7280_TOP_CFG4_FREEZE_IMPEDANCE & 0xC0U) == 0xC0U,
             "V2I_FACTOR_FREEZE and CALIB_IMPEDANCE_DIS must both be enabled");
BUILD_ASSERT((DA7280_TOP_INT_CFG1_NORMAL_BEMF_FAULT & 0x03U) == 0x01U,
             "BEMF_FAULT_LIM must remain at the normal 4.9 mV setting");

/* Validated current-loop profile:
 * IMAX=0x14, double-current range enabled, V2I=0x00D1, TRIM3=0x4E.
 */
#define DA7280_ACTUATOR_IMAX_REG 0x14
#define DA7280_V2I_FACTOR_H      0x00
#define DA7280_V2I_FACTOR_L      0xD1
#define DA7280_TRIM3_VALUE       0x4E

BUILD_ASSERT(DA7280_ACTUATOR_IMAX_REG == 0x14,
             "IMAX must remain 0x14 for the imported 345.2 mA double-range profile");
BUILD_ASSERT(DA7280_V2I_FACTOR_H == 0x00 && DA7280_V2I_FACTOR_L == 0xD1,
             "V2I factor must remain 0x00D1");
BUILD_ASSERT((DA7280_TRIM3_VALUE & BIT(6)) != 0,
             "LOOP_IDAC_DOUBLE_RANGE must remain enabled");

/* Fixed DA7280 register configuration used in the verified receiver. */
#define DA7280_TOP_CFG1_VALUE 0x10
#define DA7280_TOP_CFG2_VALUE 0x11
#define DA7280_TOP_CFG5_VALUE 0x00
#define DA7280_FEATURE_STRING "tracking=OFF acceleration=OFF rapid_stop=OFF"

/* Defaults: stimulation is specified directly as 0..100% intensity. */
#define DEFAULT_STIM_PERCENT 50U
BUILD_ASSERT(DEFAULT_STIM_PERCENT <= 100U,
             "Default stimulation percentage must be in the range 0..100");

#define DEFAULT_STIM_ABSMAX_REG \
    ((DEFAULT_STIM_PERCENT >= 100U) ? DA7280_ACTUATOR_VMAX_REG : \
     (((DEFAULT_STIM_PERCENT * DA7280_ACTUATOR_VMAX_REG) + 50U) / 100U))

#define DEFAULT_LEGACY_AMP_MSB DEFAULT_STIM_PERCENT /* Stores intensity percent */
#define DEFAULT_LEGACY_AMP_LSB 0x00
#define DEFAULT_LEGACY_CYC     35U    /* Stores duration in ms */
#define DEFAULT_LEGACY_FREQ    0x1A   /* Compatibility field; drive frequency stays at 170 Hz */

/* Default pulse duration; ETWM quantizes 35 ms to 35.36 ms. */
#define DEFAULT_STARTUP_PULSE_ON_MS 35U
BUILD_ASSERT(DEFAULT_STARTUP_PULSE_ON_MS > 0U &&
             DEFAULT_STARTUP_PULSE_ON_MS <= 255U,
             "Default startup pulse duration must fit pulse_on_ms");

/* Range exposed by the Python configuration UI */
#define MIN_CONFIG_PULSE_ON_MS  15U
#define MAX_CONFIG_PULSE_ON_MS 150U
BUILD_ASSERT(DEFAULT_STARTUP_PULSE_ON_MS >= MIN_CONFIG_PULSE_ON_MS &&
             DEFAULT_STARTUP_PULSE_ON_MS <= MAX_CONFIG_PULSE_ON_MS,
             "Default pulse duration must be inside the configurable range");

#define DEFAULT_PULSE_OFF_MS   1
#define DEFAULT_PULSE_COUNT    1

/* Runtime state */
static uint8_t legacy_amp_msb = DEFAULT_LEGACY_AMP_MSB;
static uint8_t legacy_amp_lsb = DEFAULT_LEGACY_AMP_LSB;
static uint8_t legacy_cyc     = DEFAULT_LEGACY_CYC;
static uint8_t legacy_freq    = DEFAULT_LEGACY_FREQ;

/* Derived haptic parameters */
static uint8_t pulse_level = DEFAULT_LEGACY_AMP_MSB;
static uint8_t pulse_absmax;
static uint8_t pulse_on_ms = DEFAULT_STARTUP_PULSE_ON_MS;
static uint8_t pulse_off_ms = DEFAULT_PULSE_OFF_MS;
static uint8_t pulse_count = DEFAULT_PULSE_COUNT;

static uint8_t sleep_enabled;
static uint8_t sleep_on_units;
static uint8_t sleep_off_units;

enum sleep_state {
    AWAKE,
    ASLEEP,
};

static struct {
    enum sleep_state current_state;
    uint32_t last_state_change_time_ms;
} sleep_tracker = {
    .current_state = AWAKE,
    .last_state_change_time_ms = 0,
};

/* Phase/FSM ownership is in vts_phase_runtime.c and vts_phase_link.c.
 * No phase state is mutated in an interrupt or in the haptic thread. */
static struct esb_payload rx_payload;
static struct gpio_callback phase_button_callback;
K_SEM_DEFINE(feedback_request_sem, 0, 1);
K_THREAD_STACK_DEFINE(feedback_thread_stack, 1536);
static struct k_thread feedback_thread_control;
static atomic_t feedback_requested;
static uint32_t feedback_ticket;
static uint16_t feedback_duration_ms;

/* Work items */
static void haptic_work_handler(struct k_work *work);
static void haptic_change_handler(struct k_work *work);
static void sleep_change_handler(struct k_work *work);

K_WORK_DEFINE(haptic_work, haptic_work_handler);
K_WORK_DEFINE(haptic_change_work, haptic_change_handler);
K_WORK_DEFINE(sleep_change_work, sleep_change_handler);

static struct k_sem haptic_sem;
static uint8_t pending_legacy_amp_msb;
static uint8_t pending_legacy_amp_lsb;
static uint8_t pending_legacy_cyc;
static uint8_t pending_legacy_freq;

static uint8_t pending_sleep_enabled;
static uint8_t pending_sleep_on_units;
static uint8_t pending_sleep_off_units;

/* Message queues prevent fast RX packets overwriting a configuration that
 * has not yet been processed by the system workqueue. Drop with warning if
 * an unexpected burst exceeds the available buffers.
 */
struct haptic_config_packet {
    uint8_t amplitude_percent;
    uint8_t legacy_lsb;
    uint8_t duration_ms;
    uint8_t legacy_frequency;
};
struct sleep_config_packet {
    uint8_t enabled;
    uint8_t on_units;
    uint8_t off_units;
};
K_MSGQ_DEFINE(haptic_config_queue, sizeof(struct haptic_config_packet), 8, 4);
K_MSGQ_DEFINE(sleep_config_queue, sizeof(struct sleep_config_packet), 8, 4);


static int write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_write(i2c_dev, buf, sizeof(buf), DA7280_ADDR);
}

static int read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_write_read(i2c_dev, DA7280_ADDR,
                          &reg, sizeof(reg), val, 1);
}

/* Diagnostic snapshots are READ-ONLY, taken only by the feedback thread
 * while it owns haptic_sem. No additional IRQ clears or playback writes.
 * rc is printed alongside each value to distinguish a failed read from 0.
 */
struct vts_haptic_diag_snapshot {
    uint8_t event1, warning, sequence, status1, actuator, control1;
    int event_rc, warning_rc, sequence_rc, status_rc, actuator_rc, control_rc;
};

static void vts_haptic_diag_read(struct vts_haptic_diag_snapshot *out)
{
    memset(out, 0, sizeof(*out));
    out->event_rc = read_reg(REG_IRQ_EVENT1, &out->event1);
    out->warning_rc = read_reg(REG_IRQ_WARNING_DIAG, &out->warning);
    out->sequence_rc = read_reg(REG_IRQ_SEQ_DIAG, &out->sequence);
    out->status_rc = read_reg(REG_IRQ_STATUS1, &out->status1);
    out->actuator_rc = read_reg(REG_IRQ_ACTUATOR_DIAG, &out->actuator);
    out->control_rc = read_reg(REG_TOP_CTL1, &out->control1);
}

/*
 * ETWM uses the DA7280 waveform memory with a 1.36 ms timebase.
 * The sequence ends with a zero-amplitude point.
 */
static uint8_t etwm_pwl_step(uint8_t timebases, uint8_t amp_code)
{
    return (uint8_t)((((timebases - 1U) & 0x07U) << 4) |
                     (amp_code & 0x0FU));
}

static int write_regs_sequential(uint8_t start_reg,
                                 const uint8_t *data,
                                 size_t len)
{
    size_t offset = 0U;

    while (offset < len) {
        size_t chunk = MIN(len - offset, (size_t)16U);
        uint8_t buf[17];

        buf[0] = (uint8_t)(start_reg + offset);
        memcpy(&buf[1], &data[offset], chunk);

        int rc = i2c_write(i2c_dev, buf, chunk + 1U, DA7280_ADDR);
        if (rc < 0) {
            return rc;
        }

        offset += chunk;
    }

    return 0;
}

/*
 * Map 0..100% intensity directly to ABSMAX 0x00..0x6B.
 * Examples: 0%=0x00, 50%=0x36, 100%=0x6B.
 */
static uint8_t percent_to_absmax(uint8_t percent)
{
    if (percent >= 100U) {
        return DA7280_ACTUATOR_VMAX_REG;
    }

    return (uint8_t)((((uint32_t)percent *
                       (uint32_t)DA7280_ACTUATOR_VMAX_REG) + 50U) / 100U);
}

/* Non-zero pulses use full ETWM amplitude; ABSMAX provides fine scaling. */
static void select_etwm_level(uint8_t requested_absmax,
                              uint8_t *amp_code,
                              uint8_t *gain_code)
{
    if (requested_absmax == 0U) {
        *amp_code = 0U;
        *gain_code = 0U;
        return;
    }

    *amp_code = 7U;
    *gain_code = 0U;
}

static int build_etwm_memory(uint8_t mem[DA7280_ETWM_MEM_SIZE],
                             uint16_t duration_ms, uint8_t requested_absmax)
{
    memset(mem, 0, DA7280_ETWM_MEM_SIZE);

    uint8_t amp_code;
    uint8_t gain_code;
    select_etwm_level(requested_absmax, &amp_code, &gain_code);

    /* Convert requested duration to the nearest 1.36 ms ETWM timebase. */
    uint16_t active_timebases =
        (uint16_t)(((uint32_t)duration_ms * 1000U +
                    (DA7280_ETWM_TIMEBASE_US / 2U)) /
                   DA7280_ETWM_TIMEBASE_US);

    if (active_timebases == 0U) {
        active_timebases = 1U;
    }

    /*
     * ETWM header: one snippet and one sequence.
     * Constant-amplitude points form the pulse, followed by zero.
     */
    mem[0] = 0x01U;
    mem[1] = 0x01U;

    uint8_t idx = 4U;
    uint16_t remaining = active_timebases;

    /* Reserve room for the final zero point and sequence byte. */
    if (duration_ms == 0U) {
        return -EINVAL;
    }

    /* First amplitude point */
    mem[idx++] = etwm_pwl_step(1U, amp_code);
    remaining--;

    while (remaining > 0U) {
        uint8_t n = (remaining > 8U) ? 8U : (uint8_t)remaining;
        if (idx >= (DA7280_ETWM_MEM_SIZE - 2U)) {
            return -E2BIG;
        }
        mem[idx++] = etwm_pwl_step(n, amp_code);
        remaining -= n;
    }

    /* End sequence at zero amplitude */
    mem[idx++] = etwm_pwl_step(1U, 0x00U);

    /* Snippet end pointer */
    mem[2] = (uint8_t)(idx - 1U);

    /* Sequence 0: one frame, 0 dB gain, 1.36 ms timebase, snippet 1. */
    mem[idx] = (uint8_t)((gain_code << 5) | 0x01U);

    /* Sequence end pointer */
    mem[3] = idx;

    return 0;
}

static int program_etwm_waveform_for(uint16_t duration_ms, uint8_t absmax)
{
    uint8_t mem[DA7280_ETWM_MEM_SIZE];
    int rc = build_etwm_memory(mem, duration_ms, absmax);

    if (rc < 0) {
        return rc;
    }

    /* Program waveform only while playback is inactive */
    rc = write_reg(REG_TOP_CTL1, DA7280_MODE_INACTIVE);
    if (rc < 0) {
        return rc;
    }

    rc = write_reg(REG_TOP_CTL2, 0x00);
    if (rc < 0) {
        return rc;
    }

    /* Apply the selected ABSMAX value before programming the waveform. */
    rc = write_reg(REG_ACTUATOR_ABSMAX, absmax);
    if (rc < 0) {
        return rc;
    }

    /* Enable waveform-memory writes with I2C auto-increment. */
    rc = write_reg(REG_MEM_CTL2, 0x80);
    if (rc < 0) {
        return rc;
    }

    rc = write_reg(REG_CIF_I2C1, 0x40);
    if (rc < 0) {
        (void)write_reg(REG_MEM_CTL2, 0x00);
        return rc;
    }

    rc = write_regs_sequential(REG_SNP_MEM_0, mem, sizeof(mem));

    /* Re-lock waveform memory */
    (void)write_reg(REG_MEM_CTL2, 0x00);

    if (rc < 0) {
        return rc;
    }

    /*
     * Normal-wave ETWM, sequence 0, no loop.
     * Playback is started over I2C with SEQ_START.
     */
    rc = write_reg(REG_SEQ_CTL1, 0x04);
    if (rc < 0) {
        return rc;
    }

    rc = write_reg(REG_SEQ_CTL2, 0x00);
    if (rc < 0) {
        return rc;
    }

    rc = write_reg(REG_GPI_0_CTL, 0x00);
    if (rc < 0) {
        return rc;
    }

    /* Enter ETWM standby */
    rc = write_reg(REG_TOP_CTL1, DA7280_TOP_CTL1_ETWM_STANDBY);
    if (rc < 0) {
        return rc;
    }

    return 0;
}

/* Restore the configured normal stimulus sequence (unchanged default). */
static int program_etwm_waveform(void)
{
    return program_etwm_waveform_for(pulse_on_ms, pulse_absmax);
}

/* DA7280 initialization uses the validated direct-write sequence. */
static int da7280_init(void)
{
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("DA7280 I2C bus not ready");
        return -ENODEV;
    }

    k_msleep(100);

    /* Stop output */
    if (write_reg(REG_TOP_CTL2, 0x00) < 0) {
        return -EIO;
    }

    /* Set ~170 Hz */
    if (write_reg(REG_FRQ_LRA_PER_H, DA7280_LRA_PER_170HZ_H) < 0) {
        return -EIO;
    }

    if (write_reg(REG_FRQ_LRA_PER_L, DA7280_LRA_PER_170HZ_L) < 0) {
        return -EIO;
    }

    /* Set H-bridge edge rate to 25 mV/ns */
    if (write_reg(REG_TRIM6, DA7280_TRIM6_ERC25_VALUE) < 0) {
        return -EIO;
    }

    /* Set actuator voltage limits */
    if (write_reg(REG_ACTUATOR_NOMMAX, DA7280_ACTUATOR_VMAX_REG) < 0) {
        return -EIO;
    }

    if (write_reg(REG_ACTUATOR_ABSMAX, DA7280_ACTUATOR_VMAX_REG) < 0) {
        return -EIO;
    }

    /* Set current limit */
    if (write_reg(REG_ACTUATOR_IMAX, DA7280_ACTUATOR_IMAX_REG) < 0) {
        return -EIO;
    }

    /* Set V2I factor */
    if (write_reg(REG_CALIB_V2I_H, DA7280_V2I_FACTOR_H) < 0) {
        return -EIO;
    }

    if (write_reg(REG_CALIB_V2I_L, DA7280_V2I_FACTOR_L) < 0) {
        return -EIO;
    }

    /* Set double-current-range profile */
    if (write_reg(REG_TRIM3, DA7280_TRIM3_VALUE) < 0) {
        return -EIO;
    }

    /* Apply the fixed DA7280 configuration. */

    if (write_reg(REG_TOP_CFG1, DA7280_TOP_CFG1_VALUE) < 0) {
        return -EIO;
    }

    if (write_reg(REG_TOP_CFG2, DA7280_TOP_CFG2_VALUE) < 0) {
        return -EIO;
    }

    /* Freeze V2I and disable automatic impedance-based updates. */
    if (write_reg(REG_TOP_CFG4, DA7280_TOP_CFG4_FREEZE_IMPEDANCE) < 0) {
        return -EIO;
    }

    if (write_reg(REG_TOP_CFG5, DA7280_TOP_CFG5_VALUE) < 0) {
        return -EIO;
    }

    /* Keep normal BEMF fault protection enabled. */
    if (write_reg(REG_TOP_INT_CFG1, DA7280_TOP_INT_CFG1_NORMAL_BEMF_FAULT) < 0) {
        return -EIO;
    }

    /* Program waveform and enter ETWM standby */
    if (program_etwm_waveform() < 0) {
        return -EIO;
    }

    return 0;
}

static void da7280_recover_after_fault_if_needed(void)
{
    uint8_t events;
    int rc;

    rc = read_reg(REG_IRQ_EVENT1, &events);
    if (rc < 0) {
        LOG_ERR("Failed to read DA7280 IRQ_EVENT1 after pulse, rc=%d", rc);
        return;
    }

    if (events == 0U) {
        return;
    }

    /* Ignore normal SEQ_DONE; warn on any other event. */
    uint8_t unexpected_events = (uint8_t)(events & (uint8_t)~DA7280_IRQ_SEQ_DONE);
    if (unexpected_events != 0U) {
        LOG_WRN("DA7280 event(s) after ETWM pulse: IRQ_EVENT1=0x%02X", events);
    }

    /* Clear events and restore standby */
    (void)write_reg(REG_IRQ_EVENT1, events);
    (void)write_reg(REG_TOP_CTL2, 0x00);
    (void)write_reg(REG_TOP_CTL1, DA7280_TOP_CTL1_ETWM_STANDBY);
}

static void da7280_force_off(void)
{
    if (device_is_ready(i2c_dev)) {
        /* Stop playback and return to standby */
        (void)write_reg(REG_TOP_CTL1, DA7280_TOP_CTL1_ETWM_STANDBY);
        (void)write_reg(REG_TOP_CTL2, 0x00);
    }
}

static void apply_legacy_haptic_settings(void)
{
    /* Legacy amplitude field now stores intensity percent. */
    pulse_level = legacy_amp_msb;
    if (pulse_level > 100U) {
        LOG_WRN("Stored intensity %u%% is outside 0..100%%; using default %u%%",
                pulse_level, DEFAULT_STIM_PERCENT);
        pulse_level = DEFAULT_STIM_PERCENT;
        legacy_amp_msb = DEFAULT_STIM_PERCENT;
    }

    pulse_absmax = percent_to_absmax(pulse_level);

    /* Legacy CYC field now stores pulse duration in milliseconds. */
    if (legacy_cyc < MIN_CONFIG_PULSE_ON_MS ||
        legacy_cyc > MAX_CONFIG_PULSE_ON_MS) {
        LOG_WRN("Stored duration %u ms is outside %u..%u ms; using default %u ms",
                legacy_cyc, MIN_CONFIG_PULSE_ON_MS, MAX_CONFIG_PULSE_ON_MS,
                DEFAULT_STARTUP_PULSE_ON_MS);
        legacy_cyc = DEFAULT_STARTUP_PULSE_ON_MS;
    }

    pulse_on_ms = legacy_cyc;

    /* Use a single pulse */
    pulse_off_ms = DEFAULT_PULSE_OFF_MS;
    pulse_count = DEFAULT_PULSE_COUNT;
}

/* NVS helpers */
static int nvs_mount_storage(void)
{
    int rc;
    struct flash_pages_info info;

    fs.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(fs.flash_device)) {
        LOG_ERR("Flash device %s not ready", fs.flash_device->name);
        return -ENODEV;
    }

    fs.offset = NVS_PARTITION_OFFSET;

    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) {
        LOG_ERR("Unable to get page info, rc=%d", rc);
        return rc;
    }

    fs.sector_size = info.size;
    fs.sector_count = 3U;

    rc = nvs_mount(&fs);
    if (rc) {
        LOG_ERR("NVS mount failed, rc=%d", rc);
        return rc;
    }

    return 0;
}

int vts_hw_permanent_load(vts_persist_state_t *out)
{
    /* Existing 0x5301 key, backward-read compatible with 24-byte v1 records.
     * A present malformed/unknown record MUST block CLIC startup; never
     * convert failed validation into an empty Permanent selection. */
    uint8_t raw[VTS_PERSIST_STORAGE_SIZE];
    if (!out) return -EINVAL;
    int rc=nvs_read(&fs,VTS_PERMANENT_KEY_ID,raw,sizeof(raw));
    if (rc == -ENOENT) return 0;
    if ((rc != (int)VTS_PERSIST_SIZE && rc != (int)VTS_PERSIST_V2_SIZE) ||
        !vts_persist_decode_any(raw,(size_t)(rc>0?rc:0),out)) {
        LOG_ERR("VTS_PERSIST READ_INVALID rc=%d allowed=24_or_36",rc);
        return -EIO;
    }
    return 1;
}
int vts_hw_permanent_save(const vts_phase_fsm_t *fsm)
{
    uint8_t raw[VTS_PERSIST_V2_SIZE],verify[VTS_PERSIST_V2_SIZE];
    if (!vts_persist_encode_v2(fsm,raw)) return -EINVAL;
    int rc=nvs_write(&fs,VTS_PERMANENT_KEY_ID,raw,sizeof(raw));
    if (rc < 0 || (rc != 0 && rc != (int)sizeof(raw))) return rc < 0 ? rc : -EIO;
    rc=nvs_read(&fs,VTS_PERMANENT_KEY_ID,verify,sizeof(verify));
    if (rc != (int)sizeof(raw) || memcmp(raw,verify,sizeof(raw)) != 0) return -EIO;
    return 0;
}
int vts_hw_permanent_clear(void)
{
    int rc=nvs_delete(&fs,VTS_PERMANENT_KEY_ID);
    if (rc < 0) return rc;
    uint8_t verify[VTS_PERSIST_STORAGE_SIZE];
    return nvs_read(&fs,VTS_PERMANENT_KEY_ID,verify,sizeof(verify)) == -ENOENT ? 0 : -EIO;
}

static void load_settings_from_nvs(void)
{
    /*
     * New firmware builds restore compiled haptic defaults.
     * Reboots of the same build restore the values saved in NVS.
     */
    char stored_build_id[sizeof(firmware_build_id)] = {0};
    int build_rc = nvs_read(&fs, BUILD_ID_KEY_ID,
                            stored_build_id, sizeof(stored_build_id));

    bool new_firmware =
        (build_rc != sizeof(stored_build_id)) ||
        (memcmp(stored_build_id, firmware_build_id,
                sizeof(firmware_build_id)) != 0);

    int rc = nvs_read(&fs, KEY_ID, &key, sizeof(key));

    /* rc=8: current format; rc=7: previous format without mirrored duration. */
    bool loaded_full_nvs = (rc == sizeof(key));
    bool loaded_legacy_fields = (rc == 7 || loaded_full_nvs);

    if (loaded_legacy_fields) {
        if (loaded_full_nvs) {
            LOG_INF("Loaded amplitude/duration settings from NVS");
        } else {
            LOG_INF("Loaded previous-format NVS fields; mirrored exact-duration field not present");
        }

        legacy_amp_msb  = key[0];
        legacy_amp_lsb  = key[1];
        legacy_cyc      = key[2];
        legacy_freq     = key[3];

        sleep_enabled   = key[4];
        sleep_on_units  = key[5];
        sleep_off_units = key[6];
    } else {
        LOG_INF("No valid NVS settings found; using compiled defaults");

        legacy_amp_msb  = DEFAULT_LEGACY_AMP_MSB;
        legacy_amp_lsb  = DEFAULT_LEGACY_AMP_LSB;
        legacy_cyc      = DEFAULT_LEGACY_CYC;
        legacy_freq     = DEFAULT_LEGACY_FREQ;

        sleep_enabled   = 0x00;
        sleep_on_units  = 0x00;
        sleep_off_units = 0x00;
    }

    if (new_firmware || !loaded_full_nvs) {
        /* On new/migrated builds, reset haptic defaults but preserve sleep settings. */
        LOG_INF("Restoring compiled haptic defaults: amplitude=%u%% duration=%u ms",
                DEFAULT_STIM_PERCENT, DEFAULT_STARTUP_PULSE_ON_MS);

        legacy_amp_msb = DEFAULT_LEGACY_AMP_MSB;
        legacy_amp_lsb = DEFAULT_LEGACY_AMP_LSB;
        legacy_cyc     = DEFAULT_LEGACY_CYC;
        legacy_freq    = DEFAULT_LEGACY_FREQ;
        pulse_on_ms    = DEFAULT_STARTUP_PULSE_ON_MS;
    }

    /*
     * Duration source:
     * new/migrated build -> compiled default
     * same build -> saved NVS value
     */
    apply_legacy_haptic_settings();

    if (new_firmware || !loaded_full_nvs) {
        pulse_on_ms = DEFAULT_STARTUP_PULSE_ON_MS;
    } else {
        pulse_on_ms = key[KEY_PULSE_ON_MS_INDEX];
        if (pulse_on_ms == 0U) {
            /* Fallback for invalid stored duration */
            pulse_on_ms = DEFAULT_STARTUP_PULSE_ON_MS;
        }
    }

    /* Save the current 8-byte settings record when needed. */
    if (!loaded_full_nvs || new_firmware) {
        key[0] = legacy_amp_msb;
        key[1] = legacy_amp_lsb;
        key[2] = legacy_cyc;
        key[3] = legacy_freq;
        key[4] = sleep_enabled;
        key[5] = sleep_on_units;
        key[6] = sleep_off_units;
        key[KEY_PULSE_ON_MS_INDEX] = pulse_on_ms;

        rc = nvs_write(&fs, KEY_ID, &key, sizeof(key));
        if (rc < 0) {
            LOG_ERR("Failed to write settings to NVS, rc=%d", rc);
        }
    }

    if (new_firmware) {
        rc = nvs_write(&fs, BUILD_ID_KEY_ID,
                       firmware_build_id, sizeof(firmware_build_id));
        if (rc < 0) {
            LOG_ERR("Failed to store firmware build ID in NVS, rc=%d", rc);
        }
    }

    sleep_tracker.current_state = AWAKE;
    sleep_tracker.last_state_change_time_ms = k_uptime_get_32();

    LOG_INF("Startup haptic settings: intensity=%u%% ABSMAX=0x%02X on=%u ms; firmware=%s",
            legacy_amp_msb, pulse_absmax, pulse_on_ms,
            new_firmware ? "new-build defaults" : "same-build NVS amplitude+duration");
}

/* Sleep logic */
static void check_and_update_sleep_state(void)
{
    if (sleep_enabled == 0x00) {
        sleep_tracker.current_state = AWAKE;
        sleep_tracker.last_state_change_time_ms = k_uptime_get_32();
        return;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - sleep_tracker.last_state_change_time_ms;

    switch (sleep_tracker.current_state) {
    case AWAKE:
        /* 5 min per unit */
        if (elapsed >= ((uint32_t)sleep_on_units * 300000U)) {
            sleep_tracker.current_state = ASLEEP;
            sleep_tracker.last_state_change_time_ms = now;
            LOG_INF("Entering sleep state");
        }
        break;

    case ASLEEP:
        /* 10 s per unit */
        if (elapsed >= ((uint32_t)sleep_off_units * 10000U)) {
            sleep_tracker.current_state = AWAKE;
            sleep_tracker.last_state_change_time_ms = now;
            LOG_INF("Waking from sleep state");
        }
        break;
    }
}

static void haptic_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    uint8_t repeats = (pulse_count == 0U) ? 1U : pulse_count;

    for (uint8_t i = 0; i < repeats; i++) {
        /* Start ETWM sequence 0 over I2C. */
        if (write_reg(REG_TOP_CTL1, DA7280_TOP_CTL1_ETWM_START) < 0) {
            LOG_ERR("Failed to start ETWM haptic sequence");
            goto out;
        }

        /* Wait for the configured pulse duration before returning to standby. */
        k_msleep(pulse_on_ms + 2U);

        if (write_reg(REG_TOP_CTL1, DA7280_TOP_CTL1_ETWM_STANDBY) < 0) {
            LOG_ERR("Failed to clear ETWM SEQ_START");
            goto out;
        }

        if ((i + 1U) < repeats) {
            k_msleep(pulse_off_ms);
        }
    }

out:
    da7280_force_off();
    da7280_recover_after_fault_if_needed();
    k_sem_give(&haptic_sem);
}

static void apply_haptic_config(void)
{
    /* Prevent configuration I2C writes while feedback owns the actuator. */
    k_sem_take(&haptic_sem, K_FOREVER);

    /* Validate intensity and duration before applying a haptic update. */
    if (pending_legacy_amp_msb > 100U) {
        LOG_WRN("Ignoring haptic config: intensity %u%% is outside 0..100%%",
                pending_legacy_amp_msb);
        k_sem_give(&haptic_sem);
        return;
    }

    if (pending_legacy_cyc < MIN_CONFIG_PULSE_ON_MS ||
        pending_legacy_cyc > MAX_CONFIG_PULSE_ON_MS) {
        LOG_WRN("Ignoring haptic config: duration %u ms is outside %u..%u ms",
                pending_legacy_cyc, MIN_CONFIG_PULSE_ON_MS, MAX_CONFIG_PULSE_ON_MS);
        k_sem_give(&haptic_sem);
        return;
    }

    legacy_amp_msb = pending_legacy_amp_msb;
    legacy_amp_lsb = pending_legacy_amp_lsb;
    legacy_cyc     = pending_legacy_cyc;
    legacy_freq    = pending_legacy_freq;

    apply_legacy_haptic_settings();

    /* Rebuild the ETWM waveform after amplitude or duration changes. */
    int etwm_rc = program_etwm_waveform();
    if (etwm_rc < 0) {
        LOG_ERR("Failed to update ETWM waveform, rc=%d", etwm_rc);
    }

    key[0] = legacy_amp_msb;
    key[1] = legacy_amp_lsb;
    key[2] = legacy_cyc;
    key[3] = legacy_freq;
    key[KEY_PULSE_ON_MS_INDEX] = pulse_on_ms;

    int rc = nvs_write(&fs, KEY_ID, &key, sizeof(key));
    if (rc < 0) {
        LOG_ERR("Failed to save haptic config to NVS, rc=%d", rc);
    }

    LOG_INF("Updated haptic config from 0x01: intensity=%u%% amp_lsb=0x%02X duration_ms=%u legacy_freq=0x%02X (ignored) -> ABSMAX=0x%02X on=%u ms",
            legacy_amp_msb, legacy_amp_lsb, legacy_cyc, legacy_freq,
            pulse_absmax, pulse_on_ms);
    k_sem_give(&haptic_sem);
}

static void apply_sleep_config(void)
{
    sleep_enabled = pending_sleep_enabled;
    sleep_on_units = pending_sleep_on_units;
    sleep_off_units = pending_sleep_off_units;

    key[4] = sleep_enabled;
    key[5] = sleep_on_units;
    key[6] = sleep_off_units;

    int rc = nvs_write(&fs, KEY_ID, &key, sizeof(key));
    if (rc < 0) {
        LOG_ERR("Failed to save sleep config to NVS, rc=%d", rc);
    }

    sleep_tracker.current_state = AWAKE;
    sleep_tracker.last_state_change_time_ms = k_uptime_get_32();

    LOG_INF("Updated sleep config: enable=%u on_units=%u off_units=%u",
            sleep_enabled, sleep_on_units, sleep_off_units);
}

static void haptic_change_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    struct haptic_config_packet cfg;
    while (k_msgq_get(&haptic_config_queue, &cfg, K_NO_WAIT) == 0) {
        pending_legacy_amp_msb = cfg.amplitude_percent;
        pending_legacy_amp_lsb = cfg.legacy_lsb;
        pending_legacy_cyc = cfg.duration_ms;
        pending_legacy_freq = cfg.legacy_frequency;
        apply_haptic_config();
    }
}

static void sleep_change_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    struct sleep_config_packet cfg;
    while (k_msgq_get(&sleep_config_queue, &cfg, K_NO_WAIT) == 0) {
        pending_sleep_enabled = cfg.enabled;
        pending_sleep_on_units = cfg.on_units;
        pending_sleep_off_units = cfg.off_units;
        apply_sleep_config();
    }
}

static void activate_haptic(void)
{
    if (atomic_get(&feedback_requested)) {
        /* Feedback takes priority over routine stimulation. */
        return;
    }
    if (k_sem_take(&haptic_sem, K_NO_WAIT) != 0) {
        LOG_WRN("Haptic busy; trigger ignored");
        return;
    }

    (void)k_work_submit(&haptic_work);
}

/* Copy data out of the RX FIFO before scheduling work; do not leave a
 * pointer into rx_payload that subsequent stimulus packets overwrite.
 */
static void queue_haptic_change_from_body(const uint8_t *params)
{
    struct haptic_config_packet cfg = {
        .amplitude_percent = params[0],
        .legacy_lsb = params[1],
        .duration_ms = params[2],
        .legacy_frequency = params[3],
    };
    if (k_msgq_put(&haptic_config_queue, &cfg, K_NO_WAIT) == 0) {
        (void)k_work_submit(&haptic_change_work);
    } else {
        LOG_ERR("Haptic config queue overflow");
    }
}

static void queue_sleep_change_from_body(const uint8_t *params)
{
    struct sleep_config_packet cfg = {
        .enabled = params[0],
        .on_units = params[1],
        .off_units = params[2],
    };
    if (k_msgq_put(&sleep_config_queue, &cfg, K_NO_WAIT) == 0) {
        (void)k_work_submit(&sleep_change_work);
    } else {
        LOG_ERR("Sleep config queue overflow");
    }
}

static void event_handler(struct esb_evt const *event)
{
    switch (event->evt_id) {
    case ESB_EVENT_TX_SUCCESS:
        /* A radio event never commits the phase: forward to the owner. */
        vts_runtime_ack();
        break;
    case ESB_EVENT_TX_FAILED:
        LOG_WRN("ESB ACK payload TX failure");
        break;
    case ESB_EVENT_RX_RECEIVED:
        while (esb_read_rx_payload(&rx_payload) == 0) {
            check_and_update_sleep_state();
            if (rx_payload.length < sizeof(esb_packet_header_t)) {
                LOG_WRN("Ignoring truncated packet: %u bytes", rx_payload.length);
                continue;
            }
            const uint8_t type = rx_payload.data[0];
            const uint8_t declared_length = rx_payload.data[1];
            uint8_t actual_length = rx_payload.length - sizeof(esb_packet_header_t);
            /* CLIC 5.1 observed at runtime: ping is sent as a 32-byte ESB
             * radio payload, yet its application header says 05 00.
             * Accept this exact padded-ping case without reading padding as
             * meaningful payload. All other packet types stay length-strict.
             */
            bool padded_ping = (type == (uint8_t)esb_payload_ping &&
                                declared_length == 0U &&
                                rx_payload.length == 32U);
            if (declared_length != actual_length && !padded_ping) {
                LOG_WRN("Bad packet length: type=%u declared=%u received=%u",
                        type, declared_length, actual_length);
                if (type == (uint8_t)esb_payload_command_response) {
                    LOG_HEXDUMP_INF(rx_payload.data, rx_payload.length,
                                    "VTS_DIAG MALFORMED_RESPONSE_HEX");
                }
                continue;
            }
            if (padded_ping) {
                actual_length = 0U; /* Only the declared application payload counts. */
            }
            const uint8_t *body = &rx_payload.data[sizeof(esb_packet_header_t)];
            switch (type) {
            case esb_payload_command_response:
                atomic_inc(&vts_diag_rx_response);
                LOG_INF("VTS_DIAG RESPONSE_RAW t_ms=%u pkt_len=%u body_len=%u",
                        k_uptime_get_32(), (unsigned int)rx_payload.length,
                        (unsigned int)actual_length);
                LOG_HEXDUMP_INF(rx_payload.data, rx_payload.length,
                                "VTS_DIAG RESPONSE_HEX (including type/length)");
                vts_runtime_response(body, actual_length);
                break;

            case esb_payload_ping: {
                atomic_inc(&vts_diag_rx_ping);
                if (actual_length != 0U) {
                    LOG_WRN("Invalid CLIC ping payload length %u", actual_length);
                }
                /* Activity can wake a read-only probe and drive the active
                 * clock. Activity alone never confirms a phase change. */
                if (actual_length == 0U) vts_runtime_rx_activity();
                /* ESB hardware sends the ACK; no application ping response. */
                break;
            }

            case esb_payload_eeg_sample:
                atomic_inc(&vts_diag_rx_eeg);
                /* CLIC 5.1: IEEE-754 float32 little-endian, four bytes.
                 * Data are not needed for confirmation. Never log at 256 Hz.
                 */
                if (actual_length != 4U) {
                    LOG_WRN("Invalid CLIC EEG sample length %u", actual_length);
                } else {
                    /* CLIC can stream EEG for long periods without PINGs.
                     * RX only wakes a rate-limited, read-only GET probe.
                     */
                    vts_runtime_rx_activity();
                }
                break;

            case esb_payload_stimulus:
                atomic_inc(&vts_diag_rx_stim);
                vts_runtime_rx_activity();
                if (sleep_tracker.current_state == AWAKE) {
                    activate_haptic();
                }
                break;

            case esb_payload_vts_command:
                if (actual_length < 1U) {
                    LOG_WRN("Empty VTS command body");
                    break;
                }
                if (body[0] == VTS_SUBCMD_CHANGE_HAPTIC) {
                    if (actual_length < 5U) {
                        LOG_WRN("Haptic configuration too short: %u", actual_length);
                    } else if (sleep_tracker.current_state == AWAKE) {
                        queue_haptic_change_from_body(&body[1]);
                    }
                } else if (body[0] == VTS_SUBCMD_CHANGE_SLEEP) {
                    if (actual_length < 4U) {
                        LOG_WRN("Sleep configuration too short: %u", actual_length);
                    } else {
                        queue_sleep_change_from_body(&body[1]);
                    }
                } else {
                    LOG_WRN("Unknown VTS subcommand: 0x%02x", body[0]);
                }
                break;

            default:
                LOG_DBG("Unprocessed ESB packet type %u", type);
                break;
            }
        }
        break;
    default:
        break;
    }
}

static void phase_button_gpio_isr(const struct device *port,
                                  struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(port); ARG_UNUSED(cb); ARG_UNUSED(pins);
    int level = gpio_pin_get_dt(&phase_button);
    if (level >= 0) vts_runtime_button_edge(level != 0);
}

static int phase_button_initialize(void)
{
    if (!gpio_is_ready_dt(&phase_button)) return -ENODEV;
    int rc = gpio_pin_configure_dt(&phase_button, GPIO_INPUT);
    if (rc) return rc;
    int initial_level = gpio_pin_get_dt(&phase_button);
    if (initial_level < 0) return initial_level;
    rc=vts_runtime_init(initial_level > 0);
    if (rc) return rc;
    if (initial_level > 0) LOG_WRN("Phase button held on boot; release before pressing again");
    gpio_init_callback(&phase_button_callback, phase_button_gpio_isr, BIT(phase_button.pin));
    rc = gpio_add_callback(phase_button.port, &phase_button_callback);
    if (rc) return rc;
    return gpio_pin_interrupt_configure_dt(&phase_button, GPIO_INT_EDGE_BOTH);
}

int vts_hw_submit_feedback(uint32_t ticket, uint16_t duration_ms)
{
    if (!atomic_cas(&feedback_requested, 0, 1)) return -EBUSY;
    feedback_ticket = ticket;
    feedback_duration_ms = duration_ms;
    k_sem_give(&feedback_request_sem);
    return 0;
}

static void feedback_thread_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);
    while (true) {
        k_sem_take(&feedback_request_sem, K_FOREVER);
        const uint32_t ticket = feedback_ticket;
        const uint16_t duration_ms = feedback_duration_ms;
        const uint32_t wait_begin_ms = k_uptime_get_32();
        const int stimulus_before = (int)atomic_get(&vts_diag_rx_stim);
        LOG_INF("VTS_DIAG FEEDBACK_THREAD_WAKE t_ms=%u", wait_begin_ms);
        k_sem_take(&haptic_sem, K_FOREVER);
        const uint32_t sem_wait_ms = k_uptime_get_32() - wait_begin_ms;
        /* Capture any pre-existing SEQ_DONE before reprogramming. An event
         * present before this request cannot prove the new waveform ran. */
        uint8_t pre_event = 0U;
        const int pre_rc = read_reg(REG_IRQ_EVENT1, &pre_event);
        LOG_INF("VTS_HAPTIC PRE ticket=%u sem_wait_ms=%u pre_event=0x%02X pre_rc=%d stim_rx=%d",
                ticket, sem_wait_ms, pre_event, pre_rc, stimulus_before);
        /* v1.13e experiment: allow the DA7280 to settle only when this
         * confirmation had to wait for an already-running stimulus/config.
         * Keep haptic_sem held and feedback_requested set while sleeping:
         * no routine stimulus can enter during this handover. */
        const uint32_t handover_ms = (sem_wait_ms > 0U) ? 30U : 0U;
        LOG_INF("VTS_HAPTIC HANDOFF ticket=%u sem_wait_ms=%u settle_ms=%u",
                ticket, sem_wait_ms, handover_ms);
        if (handover_ms != 0U) {
            k_msleep(handover_ms);
        }
        LOG_INF("VTS_DIAG FEEDBACK_START t_ms=%u duration_ms=%u absmax=0x%02X",
                k_uptime_get_32(), duration_ms, pulse_absmax);
        int play_rc = program_etwm_waveform_for(duration_ms, pulse_absmax);
        if (play_rc == 0) {
            play_rc = write_reg(REG_TOP_CTL1, DA7280_TOP_CTL1_ETWM_START);
            if (play_rc == 0) {
                LOG_INF("VTS_DIAG FEEDBACK_PLAYBACK_STARTED t_ms=%u", k_uptime_get_32());
                k_msleep(duration_ms + 2U);
            }
        }
        /* Sample after the original playback wait and BEFORE force-off and
         * recovery clear IRQ_EVENT1. Reads do not clear sticky W1C events.
         * Never infer physical actuator motion from SEQ_DONE alone. */
        struct vts_haptic_diag_snapshot diag;
        vts_haptic_diag_read(&diag);
        const bool seq_done = (diag.event_rc == 0) &&
                              ((diag.event1 & DA7280_IRQ_SEQ_DONE) != 0U);
        const bool fresh_done = seq_done && (pre_rc == 0) &&
                                ((pre_event & DA7280_IRQ_SEQ_DONE) == 0U);
        LOG_INF("VTS_HAPTIC POST ticket=%u event=0x%02X event_rc=%d seq_done=%u fresh_done=%u",
                ticket, diag.event1, diag.event_rc,
                seq_done ? 1U : 0U, fresh_done ? 1U : 0U);
        LOG_INF("VTS_HAPTIC FAULT ticket=%u warn=0x%02X warn_rc=%d seq=0x%02X seq_rc=%d status=0x%02X status_rc=%d",
                ticket, diag.warning, diag.warning_rc, diag.sequence,
                diag.sequence_rc, diag.status1, diag.status_rc);
        LOG_INF("VTS_HAPTIC STATE ticket=%u actuator=0x%02X act_rc=%d top_ctl1=0x%02X ctl_rc=%d stim_rx_delta=%d",
                ticket, diag.actuator, diag.actuator_rc, diag.control1,
                diag.control_rc,
                (int)atomic_get(&vts_diag_rx_stim) - stimulus_before);
        if (play_rc) LOG_ERR("Could not play confirmation feedback: %d", play_rc);
        da7280_force_off();
        da7280_recover_after_fault_if_needed();
        int restore_rc = program_etwm_waveform();
        if (restore_rc) LOG_ERR("Could not restore normal stimulus waveform: %d", restore_rc);
        LOG_INF("VTS_DIAG FEEDBACK_END t_ms=%u play_rc=%d restore_rc=%d",
                k_uptime_get_32(), play_rc, restore_rc);
        atomic_set(&feedback_requested, 0);
        k_sem_give(&haptic_sem);
        /* The same validated waveform sequence; only the owner notification
         * differs. No ISR/haptic thread can mutate the selection list. */
        vts_runtime_feedback_complete(ticket, play_rc, restore_rc);
    }
}

#if defined(CONFIG_VTS_BLE_TELEMETRY)
bool vts_hw_telemetry_idle(void)
{
    return !atomic_get(&feedback_requested) && k_sem_count_get(&haptic_sem) == 1U;
}
#endif

#if defined(CONFIG_CLOCK_CONTROL_NRF)
static int clocks_start(void)
{
    int err;
    int res;
    struct onoff_manager *clk_mgr;
    struct onoff_client clk_cli;

    clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    if (!clk_mgr) {
        LOG_ERR("Unable to get the clock manager");
        return -ENXIO;
    }

    sys_notify_init_spinwait(&clk_cli.notify);

    err = onoff_request(clk_mgr, &clk_cli);
    if (err < 0) {
        LOG_ERR("Clock request failed: %d", err);
        return err;
    }

    do {
        err = sys_notify_fetch_result(&clk_cli.notify, &res);
    } while (err == -EAGAIN);

    if (err < 0) {
        LOG_ERR("Clock result fetch failed: %d", err);
        return err;
    }
    if (res) {
        LOG_ERR("Clock could not be started: %d", res);
        return res;
    }

#if defined(NRF54L15_XXAA)
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif

    LOG_DBG("HF clock started");
    return 0;
}

#elif defined(CONFIG_CLOCK_CONTROL_NRF2)
static int clocks_start(void)
{
    int err;
    int res;
    const struct device *radio_clk_dev =
        DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
    struct onoff_client radio_cli;

    if (!radio_clk_dev) {
        LOG_ERR("Unable to get radio clock device");
        return -ENODEV;
    }

    nrf_lrcconf_poweron_force_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_1, true);
    sys_notify_init_spinwait(&radio_cli.notify);

    err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);

    do {
        err = sys_notify_fetch_result(&radio_cli.notify, &res);
    } while (err == -EAGAIN);

    if (err < 0) {
        LOG_ERR("Radio clock result fetch failed: %d", err);
        return err;
    }
    if (res) {
        LOG_ERR("Clock could not be started: %d", res);
        return res;
    }

#if defined(NRF54L15_XXAA)
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif

    LOG_DBG("HF clock started");
    return 0;
}
#else
BUILD_ASSERT(false, "No Clock Control driver");
#endif

static int esb_initialize(void)
{
    int err;

    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

    struct esb_config config = ESB_DEFAULT_CONFIG;

    config.protocol           = ESB_PROTOCOL_ESB_DPL;
    config.bitrate            = ESB_BITRATE_1MBPS;
    config.mode               = ESB_MODE_PRX;
    config.event_handler      = event_handler;
    /* A/B radio test: Rob's supplied ESB-VCOM-bridge firmware uses
     * selective_auto_ack=false. Whether CLIC PTX is configured the same
     * way is not available in the supplied CLIC 5.x binary.
     * Try false on VTS to compare with original v1.6's true setting.
     * This is NOT yet a proven fix.
     */
    config.selective_auto_ack = false;
    config.use_fast_ramp_up   = false;

    err = esb_init(&config);
    if (err) {
        return err;
    }

    err = esb_set_base_address_0(base_addr_0);
    if (err) {
        return err;
    }

    err = esb_set_base_address_1(base_addr_1);
    if (err) {
        return err;
    }

    err = esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));
    if (err) {
        return err;
    }

    err = esb_set_rf_channel(17);
    if (err) {
        return err;
    }

    LOG_INF("ESB PRX ready: bitrate=1Mbps, chan=17, fast=0, selective_auto_ack=0 (RF A/B trial)");
    return 0;
}


int main(void)
{
    int err;
    k_sem_init(&haptic_sem, 1, 1);
    if (device_is_ready(vts_diag_uart)) {
        int usb_rc = usb_enable(NULL);
        if (usb_rc != 0 && usb_rc != -EALREADY)
            LOG_ERR("VTS_DIAG USB_ENABLE_FAILED rc=%d", usb_rc);
    } else LOG_ERR("VTS_DIAG USB console device not ready");
    LOG_INF("Enhanced ShockBurst PRX receiver for DA7280 / VLV101040A (ETWM)");
    LOG_INF("Firmware condition: %s", DA7280_FEATURE_STRING);
    err = clocks_start();
    if (err) { LOG_ERR("Clock init failed: %d", err); return 0; }
    /* Explicit opt-in BLE telemetry; no-op in the ESB-only build. */
    err = vts_ble_telemetry_init();
    if (err) { LOG_ERR("VTS_BLE INIT_FAILED rc=%d", err); return 0; }
    err = esb_initialize();
    if (err) { LOG_ERR("ESB init failed: %d", err); return 0; }
    err = nvs_mount_storage();
    if (err) { LOG_ERR("NVS setup failed: %d", err); return 0; }
    load_settings_from_nvs();
    err = da7280_init();
    if (err) { LOG_ERR("DA7280 init failed: %d", err); return 0; }
    if (!phase_selection_values_valid()) { LOG_ERR("Invalid phase list"); return 0; }
    k_thread_create(&feedback_thread_control, feedback_thread_stack,
                    K_THREAD_STACK_SIZEOF(feedback_thread_stack),
                    feedback_thread_fn, NULL, NULL, NULL,
                    K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
    k_thread_name_set(&feedback_thread_control, "vts_feedback");
    /* Initialize queues/controller before enabling any input IRQ or RX. */
    err = phase_button_initialize();
    if (err) { LOG_ERR("Button P1.14 initialization failed: %d", err); return 0; }
    err = esb_start_rx();
    if (err) { LOG_ERR("RX setup failed: %d", err); return 0; }
    vts_runtime_start();
    LOG_INF("Firmware release %s", FIRMWARE_RELEASE_ID);
    LOG_INF("VTS_STAGE2 BENCH_ONLY automatic_cycling=240s BLE=%u impedance=%u",
            IS_ENABLED(CONFIG_BT) ? 1U : 0U, IS_ENABLED(CONFIG_VTS_IMPEDANCE) ? 1U : 0U);
    uint32_t next_diag_summary_ms = k_uptime_get_32() + 5000U;
    while (1) {
        check_and_update_sleep_state();
        vts_runtime_poll();
        if (device_is_ready(vts_diag_uart)) {
            uint32_t dtr = 0U;
            bool open = uart_line_ctrl_get(vts_diag_uart, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0U;
            if (open && !vts_diag_port_open) {
                LOG_INF("VTS_DIAG USB_OPEN release=v1.14a-dual-resolution-fsm mode=phase12_auto240 BLE=%u",
                        IS_ENABLED(CONFIG_BT) ? 1U : 0U);
                LOG_INF("VTS_FSM CONFIG dwell_ms=240000 inactivity_timeout_ms=%u idle_get_ms=%u feedback_ms=%u",
                        VTS_LINK_ACTIVITY_TIMEOUT_MS, VTS_LINK_IDLE_POLL_MS, VTS_LINK_FEEDBACK_MS);
                vts_runtime_status("STATUS");
                next_diag_summary_ms = k_uptime_get_32() + 5000U;
            }
            vts_diag_port_open = open;
            uint32_t now = k_uptime_get_32();
            if (open && (int32_t)(now - next_diag_summary_ms) >= 0) {
                LOG_INF("VTS_DIAG COUNTERS t_ms=%u ping=%d eeg=%d response=%d stimulus=%d",
                        now, (int)atomic_get(&vts_diag_rx_ping), (int)atomic_get(&vts_diag_rx_eeg),
                        (int)atomic_get(&vts_diag_rx_response), (int)atomic_get(&vts_diag_rx_stim));
                vts_runtime_status("STATUS");
                next_diag_summary_ms = now + 5000U;
            }
        }
        k_sleep(K_MSEC(VTS_LINK_SERVICE_MS));
    }
    return 0;
}
