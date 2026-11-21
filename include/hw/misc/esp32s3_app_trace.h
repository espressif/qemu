/*
 * ESP32-S3 AppTrace JTAG host emulation
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#ifndef HW_MISC_ESP32S3_APP_TRACE_H
#define HW_MISC_ESP32S3_APP_TRACE_H

#include "exec/memory.h"
#include "qom/object.h"

#define TYPE_ESP32S3_APP_TRACE "esp32s3-app-trace"
OBJECT_DECLARE_SIMPLE_TYPE(Esp32s3AppTraceState, ESP32S3_APP_TRACE)

/*
 * Create an AppTrace host service.
 *
 * @mode must be "file_io" (the only supported mode).
 * @base_dir, if non-NULL, is treated as a sandbox root that is prefixed to
 * every guest-supplied file path; '..' segments are rejected so the guest
 * cannot escape it. If NULL, guest paths are used verbatim, which grants the
 * guest read/write access to the host filesystem with the privileges of the
 * QEMU process (intended for coverage dumps to absolute host build paths).
 *
 * Returns NULL and sets @errp on failure.
 */
Esp32s3AppTraceState *esp32s3_app_trace_new(const char *mode,
                                           const char *base_dir, Error **errp);

/*
 * The TRAX and PERFMON register banks the AppTrace firmware drives live in
 * this window of a core's External Register (RER/WER) address space.
 */
#define ESP32S3_APP_TRACE_ER_BASE  0x100000
#define ESP32S3_APP_TRACE_ER_SIZE  0x2000

/*
 * Return the MemoryRegion emulating the TRAX/PERFMON registers of the CPU
 * with the given @core_id. Map it at ESP32S3_APP_TRACE_ER_BASE into that
 * CPU's xtensa_get_er_region(). The returned region is owned by @s.
 */
MemoryRegion *esp32s3_app_trace_core_region(Esp32s3AppTraceState *s,
                                            unsigned core_id);

#endif /* HW_MISC_ESP32S3_APP_TRACE_H */
