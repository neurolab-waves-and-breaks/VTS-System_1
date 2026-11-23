/* Adapted from Huru's nRF5 API example to nRF Connect SDK / Zephyr ESB. */
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <esb.h>
#include "clic.h"
#include "phase_wire.h"

LOG_MODULE_DECLARE(esb_prx_da7280);

void clic_process_response(const uint8_t *response, uint32_t length)
{
    /* Response body semantics remain undocumented; do not assume success. */
    LOG_HEXDUMP_INF(response, length, "CLIC command response (raw)");
}

/* Port of Rob's generic five-byte command sender to Zephyr ESB.
 * esb_write_payload() on a PRX queues an ACK payload. The PTX bridge
 * must send an ACK-requesting packet to retrieve and forward the command.
 */
int clic_send_command(const uint8_t command[CLIC_CMD_BODY_BYTES])
{
    if (command == NULL) {
        return -EINVAL;
    }
    struct esb_payload tx = {0};
    tx.pipe = ESB_PIPE_VTS;
    tx.length = (uint8_t)(sizeof(esb_packet_header_t) + CLIC_CMD_BODY_BYTES);
    tx.data[0] = (uint8_t)esb_payload_command;
    tx.data[1] = (uint8_t)CLIC_CMD_BODY_BYTES;
    memcpy(&tx.data[sizeof(esb_packet_header_t)], command, CLIC_CMD_BODY_BYTES);

    int err = esb_write_payload(&tx);
    if (err) {
        LOG_ERR("Cannot queue CLIC opcode %u in ESB ACK FIFO: %d", command[0], err);
    } else {
        LOG_INF("Queued CLIC opcode %u in ESB ACK FIFO", command[0]);
    }
    return err;
}

/* Signed int32 degrees. The pure helper is independently host-tested. */
int clic_send_phase(int32_t phase_deg)
{
    uint8_t command[CLIC_CMD_BODY_BYTES];
    if (!vts_pack_set_phase(phase_deg, command)) return -ERANGE;
    return clic_send_command(command);
}

int clic_get_phase(void)
{
    uint8_t command[CLIC_CMD_BODY_BYTES];
    vts_pack_get_phase(command);
    return clic_send_command(command);
}

/* Unsigned 32-bit big-endian frequency in Hz: exactly matches the supplied
 * Python app's struct.pack('>I', oscilltrack_frequency) for opcode 28.
 * This test build permits only 10 or 40 Hz.
 */
int clic_send_frequency(uint32_t frequency_hz)
{
    if (frequency_hz != 10U && frequency_hz != 40U) {
        return -ERANGE;
    }

    const uint8_t command[CLIC_CMD_BODY_BYTES] = {
        (uint8_t)PT10_CMD_SET_OSCILLTRACK_FREQUENCY,
        (uint8_t)((frequency_hz >> 24) & 0xFFU),
        (uint8_t)((frequency_hz >> 16) & 0xFFU),
        (uint8_t)((frequency_hz >> 8) & 0xFFU),
        (uint8_t)(frequency_hz & 0xFFU),
    };
    return clic_send_command(command);
}

/* Legacy read-only frequency GET helper (not used in v1.8 phase mode). */
int clic_get_frequency(void)
{
    const uint8_t command[CLIC_CMD_BODY_BYTES] = {
        (uint8_t)PT10_CMD_GET_OSCILLTRACK_FREQUENCY, 0, 0, 0, 0
    };
    return clic_send_command(command);
}
