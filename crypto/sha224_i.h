/*
 * SHA-224 internal definitions
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SHA224_I_H
#define SHA224_I_H

#include "qemu/osdep.h"
#include "sha256_i.h"

/**
 * @brief Size of SHA224 hash in bytes
 */
#define SHA224_HASH_SIZE 28

typedef struct sha256_state sha224_state;

void sha224_init(sha224_state *md);
int sha224_compress(sha224_state *md, unsigned char *buf);

#endif /* SHA224_I_H */
