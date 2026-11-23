#include "clic_reply.h"

bool vts_clic_reply_decode(const uint8_t *body, size_t length,
                           uint8_t *opcode, uint32_t *be_parameter)
{
    if (body == NULL || opcode == NULL || be_parameter == NULL ||
        length != VTS_CLIC_REPLY_BODY_BYTES) {
        return false;
    }
    *opcode = body[0];
    *be_parameter = ((uint32_t)body[1] << 24) |
                    ((uint32_t)body[2] << 16) |
                    ((uint32_t)body[3] << 8) |
                    (uint32_t)body[4];
    return true;
}
