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

#include "exec/hwaddr.h"
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
 * Return the per-core opaque to install as CPUXtensaState.er_opaque for the
 * CPU with the given @core_id. The returned pointer is owned by @s.
 */
void *esp32s3_app_trace_core_context(Esp32s3AppTraceState *s, unsigned core_id);

uint64_t esp32s3_app_trace_er_read(void *opaque, hwaddr addr, unsigned size);
void esp32s3_app_trace_er_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size);

#endif /* HW_MISC_ESP32S3_APP_TRACE_H */
