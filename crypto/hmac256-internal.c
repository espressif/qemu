/*
 * HMAC-SHA-224/256/384/512 implementation
 * Last update: 06/15/2005
 * Issue date:  06/15/2005
 *
 * Copyright (C) 2005 Olivier Gay <olivier.gay@a3.epfl.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>

#include "hmac256_i.h"


void hmac_sha256_init(struct hmac_sha256_ctx *ctx, const uint8_t *key,
                      uint32_t key_size)
{
    uint32_t fill = SHA256_BLOCK_SIZE - key_size;

    uint8_t block_ipad[SHA256_BLOCK_SIZE];
    uint8_t block_opad[SHA256_BLOCK_SIZE];

    memset(block_ipad + key_size, 0x36, fill);
    memset(block_opad + key_size, 0x5c, fill);

    for (int i = 0; i < (int) key_size; i++) {
        block_ipad[i] = key[i] ^ 0x36;
        block_opad[i] = key[i] ^ 0x5c;
    }

    sha256_init(&ctx->ctx_inside);
    sha256_compress(&ctx->ctx_inside, block_ipad);

    sha256_init(&ctx->ctx_outside);
    sha256_compress(&ctx->ctx_outside, block_opad);
}

void hmac_sha256_update(struct hmac_sha256_ctx *ctx, uint8_t *message)
{
    sha256_compress(&ctx->ctx_inside, message);
}


static inline void write32_be(uint32_t n, uint8_t out[4])
{
    *(uint32_t *)(out) = __builtin_bswap32(n);
}


void hmac_sha256_final(struct hmac_sha256_ctx *ctx, uint8_t *mac,
                       uint32_t mac_size)
{
    uint8_t block[SHA256_BLOCK_SIZE] = {0};
    for(int i = 0 ; i < 8; i++) {
        write32_be(ctx->ctx_inside.state[i], block + i * 4);
    }
    // As the "ctx_inside" state will be the input of the SHA operation over "ctx_outside"
    // we need to pad the "ctx_inside" state because `sha256_compress` expects a padded input
    uint64_t bit_len = __builtin_bswap64(SHA256_DIGEST_SIZE * 8 + 512);
    block[SHA256_DIGEST_SIZE] = 0x80;
    memset(block + SHA256_DIGEST_SIZE + 1, 0, SHA256_BLOCK_SIZE - SHA256_DIGEST_SIZE - 1);
    memcpy(block + SHA256_BLOCK_SIZE - sizeof(bit_len), &bit_len, sizeof(bit_len));

    sha256_compress(&ctx->ctx_outside, block);
    memcpy(mac, ctx->ctx_outside.state, SHA256_DIGEST_SIZE);
}
