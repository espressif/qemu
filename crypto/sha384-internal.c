/*
 * SHA-384 hash implementation and interface functions
 * Copyright (c) 2015, Pali Rohár <pali.rohar@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "sha384_i.h"

/* ===== start - public domain SHA384 implementation ===== */

/*
 * This is based on SHA384 implementation in LibTomCrypt that was released into
 * public domain by Tom St Denis.
 */

#define CONST64(n) n ## ULL

/*
 * Initialize the hash state
 * @param md   The hash state you wish to initialize
 * @return CRYPT_OK if successful
 */
void sha384_init(struct sha512_state *md)
{
    md->state[0] = CONST64(0xcbbb9d5dc1059ed8);
    md->state[1] = CONST64(0x629a292a367cd507);
    md->state[2] = CONST64(0x9159015a3070dd17);
    md->state[3] = CONST64(0x152fecd8f70e5939);
    md->state[4] = CONST64(0x67332667ffc00b31);
    md->state[5] = CONST64(0x8eb44a8768581511);
    md->state[6] = CONST64(0xdb0c2e0d64f98fa7);
    md->state[7] = CONST64(0x47b5481dbefa4fa4);
}

/* ===== end - public domain SHA384 implementation ===== */
