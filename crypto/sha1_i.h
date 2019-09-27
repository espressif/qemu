/*
 * SHA1 internal definitions
 * Copyright (c) 2003-2005, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SHA1_I_H
#define SHA1_I_H
#include "qemu/osdep.h"

struct sha1_state {
    uint32_t state[5];
    uint32_t count[2];
};

void sha1_init(struct sha1_state *context);
void sha1_compress(uint32_t state[5], const unsigned char buffer[64]);

#endif /* SHA1_I_H */
