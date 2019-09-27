/*
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu/host-utils.h"

#include "core-esp32/core-isa.h"
#include "overlay_tool.h"

#define xtensa_modules xtensa_modules_esp32
#include "core-esp32/xtensa-modules.inc.c"

static const XtensaOpcodeTranslators* esp32_opcode_translators[] = {
    &xtensa_core_opcodes,
    &xtensa_fpu_opcodes,
    NULL
};

static XtensaConfig xtensa_core_esp32 __attribute__((unused)) = {
    .name = "esp32",
    .gdb_regmap = {
        .reg = {
#include "core-esp32/gdb-config.inc.c"
        }
    },
    .isa_internal = &xtensa_modules,
    .clock_freq_khz = 40000,
    .opcode_translators = esp32_opcode_translators,
    DEFAULT_SECTIONS
};

REGISTER_CORE(xtensa_core_esp32)
