/*
 * SHA-224 hash implementation and interface functions
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "sha224_i.h"


int sha224_compress(sha224_state *md, unsigned char *buf)
{
    return sha256_compress(md, buf);
}


void sha224_init(sha224_state *md)
{
    /**
     * Initial values for SHA224 algorithm
     */
    md->state[0] = 0xC1059ED8UL;
    md->state[1] = 0x367CD507UL;
    md->state[2] = 0x3070DD17UL;
    md->state[3] = 0xF70E5939UL;
    md->state[4] = 0xFFC00B31UL;
    md->state[5] = 0x68581511UL;
    md->state[6] = 0x64F98FA7UL;
    md->state[7] = 0xBEFA4FA4UL;
}
