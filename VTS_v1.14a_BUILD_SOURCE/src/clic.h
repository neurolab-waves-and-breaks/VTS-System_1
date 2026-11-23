#ifndef CLIC_H_INCLUDED
#define CLIC_H_INCLUDED


#include <stdint.h>

typedef enum {
    PT10_CMD_GET_OSCILLTRACK_FREQUENCY = 27,
    PT10_CMD_SET_OSCILLTRACK_FREQUENCY = 28,
    PT10_CMD_GET_OSCILLTRACK_TRIGGER_PHASE = 29,
    PT10_CMD_SET_OSCILLTRACK_TRIGGER_PHASE = 30,
    PT10_CMD_GET_OSCILLTRACK_SUPPRESSION_DUTY_CYCLE = 31,
    PT10_CMD_SET_OSCILLTRACK_SUPPRESSION_DUTY_CYCLE = 32,
    PT10_CMD_STORE_OSCILLTRACK_PARAMETERS = 33,
    PT10_CMD_GET_OSCILLTRACK_FLAGS = 34,
    PT10_CMD_SET_OSCILLTRACK_FLAGS = 35,
    PT10_CMD_GET_CONVERGENCE_GAIN = 36,
    PT10_CMD_SET_CONVERGENCE_GAIN = 37,
    PT10_CMD_CONFIGURE_VTS_HAPTIC = 38,
    PT10_CMD_CONFIGURE_VTS_SLEEP = 39,
} pt10_command_e;

typedef enum {
    esb_payload_command = 1,
    esb_payload_command_response = 2,
    esb_payload_oscilltrack_data = 3,
    esb_payload_timeout = 4,
    esb_payload_ping = 5,
    esb_payload_ping_response = 6,
    esb_payload_bridge_command = 7,
    esb_payload_vts_command = 8,
    esb_payload_stimulus = 9,
    esb_payload_eeg_sample = 10,
} esb_payload_type_e;

#define ESB_PIPE_VTS (0)
typedef struct __attribute__ ((__packed__)) {
    uint8_t payload_type;
    uint8_t payload_length;
} esb_packet_header_t;

/* CLIC command body: exactly 5 opaque bytes, opcode + 4 parameter bytes.
 * The caller packs parameters according to each command's wire format.
 * For OSCILLTRACK_TRIGGER_PHASE, this is signed 32-bit big-endian degrees.
 */
#define CLIC_CMD_PARAMETER_BYTES 4U
#define CLIC_CMD_BODY_BYTES (1U + CLIC_CMD_PARAMETER_BYTES)

void clic_process_response(const uint8_t *response, uint32_t length);
/* Packet transport: command[0..4] already encoded; does not reinterpret bytes. */
int clic_send_command(const uint8_t command[CLIC_CMD_BODY_BYTES]);
/* Opcode 30; phase must be in [-180, +180] whole degrees. */
int clic_send_phase(int32_t phase_deg);
/* Opcode 29: signed 32-bit big-endian phase readback, no mutation. */
int clic_get_phase(void);
/* Opcode 28: frequency in Hz encoded as unsigned 32-bit big-endian.
 * This frequency-test build accepts only 10 or 40 Hz.
 */
int clic_send_frequency(uint32_t frequency_hz);
/* Opcode 27: read-only GET with zero parameter (does not change CLIC). */
int clic_get_frequency(void);

#endif /* CLIC_H_INCLUDED */