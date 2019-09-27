/*
 * SHA-384 internal definitions
 * Copyright (c) 2015, Pali Rohár <pali.rohar@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef SHA384_I_H
#define SHA384_I_H

#include "qemu/osdep.h"
#include "sha512_i.h"

#define sha384_state sha512_state

void sha384_init(struct sha384_state *md);

#endif /* SHA384_I_H */
