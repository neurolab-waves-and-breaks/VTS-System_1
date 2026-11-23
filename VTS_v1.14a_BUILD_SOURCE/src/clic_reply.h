#ifndef VTS_CLIC_REPLY_H_INCLUDED
#define VTS_CLIC_REPLY_H_INCLUDED
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* These are application payload bytes AFTER the 2-byte ESB header.
 * Python monitor v5.1 expects opcode + 4 parameter bytes for command responses.
 */
#define VTS_CLIC_REPLY_BODY_BYTES 5U
bool vts_clic_reply_decode(const uint8_t *body, size_t length,
                           uint8_t *opcode, uint32_t *be_parameter);
#endif
