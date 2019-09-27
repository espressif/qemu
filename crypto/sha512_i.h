/*
 * SHA-512 internal definitions
 * Copyright (c) 2015, Pali Rohár <pali.rohar@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SHA512_I_H
#define SHA512_I_H
#include "qemu/osdep.h"

struct sha512_state {
    uint64_t state[8];
};

void sha512_init(struct sha512_state *md);
int sha512_compress(struct sha512_state *md, unsigned char *buf);

#endif /* SHA512_I_H */
