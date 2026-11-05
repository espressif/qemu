/*
 * Helpers shared by the ESP ECC and ECDSA models
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#pragma once

#include <gcrypt.h>

/* Returns the libgcrypt name of a curve and its size in bytes, or NULL for a curve not modelled */
const char *esp_ecc_curve_name(uint32_t curve, size_t *len);

/* The hardware keeps big numbers as little-endian bytes in 32-bit registers */
gcry_mpi_t esp_ecc_mpi_from_le(const uint32_t *mem, size_t len);
void esp_ecc_mpi_to_le(gcry_mpi_t value, uint32_t *mem, size_t len);
gcry_mpi_point_t esp_ecc_point_from_le(const uint32_t *x, const uint32_t *y, size_t len);
