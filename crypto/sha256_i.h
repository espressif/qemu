/*
 * SHA-256 internal definitions
 * Copyright (c) 2003-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SHA256_I_H
#define SHA256_I_H
#include "qemu/osdep.h"

struct sha256_state {
    uint32_t state[8];
};

void sha256_init(struct sha256_state *md);
int sha256_compress(struct sha256_state *md, unsigned char *buf);

#endif /* SHA256_I_H */
