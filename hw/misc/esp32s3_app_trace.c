/*
 * ESP32-S3 AppTrace JTAG host emulation
 *
 * Emulates the OpenOCD JTAG host for the ESP-IDF AppTrace component,
 * enabling esp_gcov_dump() to stream .gcda files out of QEMU via the
 * AppTrace file I/O protocol without a real JTAG connection.
 *
 * Protocol references:
 *   Firmware side: components/app_trace/port/xtensa/port_jtag.c
 *   Host side: OpenOCD src/target/espressif/esp_apptrace.c
 *   Chunk framing: components/app_trace/app_trace_membufs_proto.c
 *   File I/O cmds: components/app_trace/host_file_io.c
 *
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/cpu-common.h"
#include "sysemu/reset.h"
#include "hw/misc/esp32s3_app_trace.h"
#include "trace.h"

/* ERI addresses (accessed via RER/WER Xtensa instructions) */
#define ERI_TRAX_DELAYCNT   0x10001CU   /* CTRL register */
#define ERI_TRAX_TRIGGERPC  0x100014U   /* STAT register */
#define ERI_PERFMON_PM1     0x101084U   /* CRC16 register */

/* CTRL register bits */
#define CTRL_BLOCK_LEN_MASK  0x7FFFU
#define CTRL_BLOCK_ID_SHIFT  15
#define CTRL_HOST_DATA       (1U << 22)
#define CTRL_HOST_CONNECT    (1U << 23)

/*
 * TRAX block physical addresses in emulated DRAM (ESP32-S3).
 * TRACEMEM_MUX_BLK0_NUM=22: 0x3FC90000 + 0x4000*(22-6) = 0x3FCD0000
 * TRACEMEM_MUX_BLK1_NUM=26: 0x3FC90000 + 0x4000*(26-6) = 0x3FCE0000
 */
#define TRACEMEM_BLK0_ADDR  0x3FCD0000U
#define TRACEMEM_BLK1_ADDR  0x3FCE0000U
#define TRACEMEM_BLK_SIZE   0x4000U

/*
 * Up-channel chunk header (4 bytes, 32-bit target):
 *   [uint16_t block_sz_16 = (core_id<<15)|data_size]
 *   [uint16_t wr_sz_16    = 0(uncommitted) or block_sz_16(committed)]
 * Skip chunk if wr_sz_16 == 0.
 */
#define CHUNK_HDR_SIZE  4

/*
 * Down-channel response header written at the start of the same block:
 *   [uint16_t block_sz = number of response bytes following]
 *   [response_data[block_sz]]
 */

/* File I/O command bytes (from host_file_io.c) */
#define CMD_FOPEN   0x00
#define CMD_FCLOSE  0x01
#define CMD_FWRITE  0x02
#define CMD_FREAD   0x03
#define CMD_FSEEK   0x04
#define CMD_FTELL   0x05
#define CMD_FSTOP   0x06
#define CMD_FEOF    0x07

/* ESP32-S3 is dual-core; each core has its own TRAX register bank. */
#define ESP32S3_APP_TRACE_MAX_CORES  2

typedef struct {
    uint32_t ctrl_reg;
    uint32_t stat_reg;
    uint32_t crc_reg;
} Esp32s3AppTraceCoreRegs;

/*
 * Per-CPU opaque installed as CPUXtensaState.er_opaque. Carries a back
 * pointer to the shared service plus this core's own TRAX register bank.
 */
typedef struct {
    Esp32s3AppTraceState *app_trace;
    Esp32s3AppTraceCoreRegs regs;
} Esp32s3AppTraceCtx;

struct Esp32s3AppTraceState {
    Object parent_obj;

    char *base_dir;             /* sandbox root, or NULL for verbatim paths */
    GHashTable *file_map;       /* uint32_t token -> FILE* (shared service) */
    uint32_t next_token;
    Esp32s3AppTraceCtx ctx[ESP32S3_APP_TRACE_MAX_CORES];
};

/* ------------------------------------------------------------------ */
/* Path resolution / sandbox confinement                              */
/* ------------------------------------------------------------------ */

/*
 * Map a guest-supplied path to a host path. With no sandbox root the path is
 * used verbatim. With a root, the path is prefixed by it and any ".." segment
 * is rejected so the guest cannot escape the root. Returns NULL on rejection.
 */
static char *app_trace_resolve_path(Esp32s3AppTraceState *s, const char *path)
{
    if (!s->base_dir) {
        return g_strdup(path);
    }

    char **parts = g_strsplit(path, "/", -1);
    for (char **p = parts; *p; p++) {
        if (strcmp(*p, "..") == 0) {
            g_strfreev(parts);
            return NULL;
        }
    }
    g_strfreev(parts);

    return g_build_filename(s->base_dir, path, NULL);
}

/* ------------------------------------------------------------------ */
/* Response buffer helpers                                             */
/* ------------------------------------------------------------------ */

static void append_u32(GByteArray *arr, uint32_t val)
{
    uint8_t buf[4];
    stl_le_p(buf, val);
    g_byte_array_append(arr, buf, 4);
}

/* ------------------------------------------------------------------ */
/* File I/O command handlers                                           */
/* args/len: payload after the command byte                            */
/* ------------------------------------------------------------------ */

static void cmd_fopen(Esp32s3AppTraceState *s,
                      const uint8_t *args, uint32_t len, GByteArray *resp)
{
    /* args: path\0 mode\0 */
    const char *path = (const char *)args;
    size_t path_len = strnlen(path, len);
    if (path_len >= len) {
        append_u32(resp, 0);
        return;
    }
    const char *mode = (const char *)args + path_len + 1;
    uint32_t mode_avail = len - (uint32_t)path_len - 1;
    if (strnlen(mode, mode_avail) >= mode_avail) {
        append_u32(resp, 0);
        return;
    }

    char *host_path = app_trace_resolve_path(s, path);
    if (!host_path) {
        append_u32(resp, 0);
        return;
    }

    char *dir = g_path_get_dirname(host_path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    FILE *f = fopen(host_path, mode);
    g_free(host_path);
    if (!f) {
        append_u32(resp, 0);
        return;
    }

    uint32_t token;
    do {                        /* token 0 is reserved for "no file" */
        token = ++s->next_token;
    } while (token == 0);
    g_hash_table_insert(s->file_map, GUINT_TO_POINTER(token), f);
    append_u32(resp, token);
}

/*
 * Decode the leading uint32 token argument and resolve it to an open FILE*.
 * Returns NULL when the payload is too short to hold a token or the token is
 * unknown; *token is set whenever len >= 4. Every caller emits a reply on the
 * NULL path, so a malformed command never leaves the firmware without a
 * response.
 */
static FILE *file_for_token(Esp32s3AppTraceState *s, const uint8_t *args,
                            uint32_t len, uint32_t *token)
{
    if (len < 4) {
        return NULL;
    }
    *token = ldl_le_p(args);
    return g_hash_table_lookup(s->file_map, GUINT_TO_POINTER(*token));
}

static void cmd_fclose(Esp32s3AppTraceState *s,
                       const uint8_t *args, uint32_t len, GByteArray *resp)
{
    uint32_t token = 0;
    FILE *f = file_for_token(s, args, len, &token);
    int32_t result = -1;
    if (f) {
        result = fclose(f);
        /*
         * steal (not remove): the file is already closed, so skip the
         * map's close-on-destroy notify
         */
        g_hash_table_steal(s->file_map, GUINT_TO_POINTER(token));
    }
    append_u32(resp, (uint32_t)result);
}

static void cmd_fwrite(Esp32s3AppTraceState *s,
                       const uint8_t *args, uint32_t len, GByteArray *resp)
{
    /* args: uint32_t token, uint8_t data[len-4] */
    uint32_t token = 0;
    FILE *f = file_for_token(s, args, len, &token);
    uint32_t result = 0;
    if (f) {
        const uint8_t *data = args + 4;
        uint32_t data_len = len - 4;
        if (data_len == 0 || fwrite(data, 1, data_len, f) == data_len) {
            result = 1;  /* firmware checks: resp == 1 ? nmemb : 0 */
        }
    }
    append_u32(resp, result);
}

static void cmd_fread(Esp32s3AppTraceState *s,
                      const uint8_t *args, uint32_t len, GByteArray *resp)
{
    /* args: uint32_t token, uint32_t size */
    if (len < 8) {
        append_u32(resp, 0);    /* n_read = 0, no data follows */
        return;
    }
    uint32_t token = 0;
    FILE *f = file_for_token(s, args, len, &token);
    uint32_t size = ldl_le_p(args + 4);
    /* Bound the guest-controlled size to one TRAX block (see writeback). */
    if (size > TRACEMEM_BLK_SIZE) {
        size = TRACEMEM_BLK_SIZE;
    }
    uint8_t *buf = g_malloc(size);
    size_t n = f ? fread(buf, 1, size, f) : 0;
    /* firmware calls rsp_recv twice: first n_read, then n_read bytes */
    append_u32(resp, (uint32_t)n);
    g_byte_array_append(resp, buf, n);
    g_free(buf);
}

static void cmd_fseek(Esp32s3AppTraceState *s,
                      const uint8_t *args, uint32_t len, GByteArray *resp)
{
    /* args: uint32_t token, int32_t offset, int32_t whence */
    if (len < 12) {
        append_u32(resp, (uint32_t)-1);
        return;
    }
    uint32_t token = 0;
    FILE *f = file_for_token(s, args, len, &token);
    int32_t offset = (int32_t)ldl_le_p(args + 4);
    int32_t whence = (int32_t)ldl_le_p(args + 8);
    int32_t result = f ? fseek(f, offset, whence) : -1;
    append_u32(resp, (uint32_t)result);
}

static void cmd_ftell(Esp32s3AppTraceState *s,
                      const uint8_t *args, uint32_t len, GByteArray *resp)
{
    uint32_t token = 0;
    FILE *f = file_for_token(s, args, len, &token);
    int32_t result = f ? (int32_t)ftell(f) : -1;
    append_u32(resp, (uint32_t)result);
}

static void cmd_feof(Esp32s3AppTraceState *s,
                     const uint8_t *args, uint32_t len, GByteArray *resp)
{
    uint32_t token = 0;
    FILE *f = file_for_token(s, args, len, &token);
    int32_t result = f ? feof(f) : 1;
    append_u32(resp, (uint32_t)result);
}

/* ------------------------------------------------------------------ */
/* Command dispatch                                                    */
/* data[0] = cmd byte; data[1..len-1] = args                          */
/* ------------------------------------------------------------------ */

static void dispatch_cmd(Esp32s3AppTraceState *s,
                         const uint8_t *data, uint32_t len, GByteArray *resp)
{
    if (len == 0) {
        return;
    }
    uint8_t cmd = data[0];
    const uint8_t *args = data + 1;
    uint32_t args_len = len - 1;

    trace_esp32s3_app_trace_cmd(cmd, args_len);

    switch (cmd) {
    case CMD_FOPEN:
        cmd_fopen(s, args, args_len, resp);
        break;
    case CMD_FCLOSE:
        cmd_fclose(s, args, args_len, resp);
        break;
    case CMD_FWRITE:
        cmd_fwrite(s, args, args_len, resp);
        break;
    case CMD_FREAD:
        cmd_fread(s, args, args_len, resp);
        break;
    case CMD_FSEEK:
        cmd_fseek(s, args, args_len, resp);
        break;
    case CMD_FTELL:
        cmd_ftell(s, args, args_len, resp);
        break;
    case CMD_FSTOP:
        /* no args, no response */
        break;
    case CMD_FEOF:
        cmd_feof(s, args, args_len, resp);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Block swap processing (triggered by swap_end WER on ERI_TRAX_DELAYCNT) */
/* ------------------------------------------------------------------ */

static void process_swap_end(Esp32s3AppTraceState *s,
                             Esp32s3AppTraceCoreRegs *regs, uint32_t val)
{
    uint32_t block_id  = (val >> CTRL_BLOCK_ID_SHIFT) & 0x7FU;
    uint32_t block_len = val & CTRL_BLOCK_LEN_MASK;

    trace_esp32s3_app_trace_swap(block_id, block_len);

    if (block_len == 0) {
        /*
         * Empty flush swap - firmware draining the down-channel response.
         * ACK with HOST_CONNECT; clear HOST_DATA since response was consumed.
         */
        regs->ctrl_reg = CTRL_HOST_CONNECT | (block_id << CTRL_BLOCK_ID_SHIFT);
        return;
    }

    /*
     * Read the up-channel block from emulated DRAM.
     * block_id%2==1 -> BLK0 (s_trax_blocks[0]); ==0 -> BLK1 (s_trax_blocks[1]).
     * Matches esp_apptrace_trax_buffer_swap: s_trax_blocks[!(new_block_id%2)].
     */
    hwaddr blk_addr =
        (block_id % 2 == 1) ? TRACEMEM_BLK0_ADDR : TRACEMEM_BLK1_ADDR;

    if (block_len > TRACEMEM_BLK_SIZE) {
        block_len = TRACEMEM_BLK_SIZE;
    }
    uint8_t *buf = g_malloc(block_len);
    cpu_physical_memory_read(blk_addr, buf, block_len);

    /* Walk committed up-channel chunks and collect responses */
    GByteArray *resp = g_byte_array_new();
    uint32_t offset = 0;
    while (offset + CHUNK_HDR_SIZE <= block_len) {
        /* block_sz_16: bits[15]=core_id, bits[14:0]=data_size (unused here) */
        uint16_t wr_sz_16 = lduw_le_p(buf + offset + 2);
        if (wr_sz_16 == 0) {
            break;  /* uncommitted chunk — end of valid data */
        }
        uint16_t data_size = wr_sz_16 & 0x7FFFU;
        if (offset + CHUNK_HDR_SIZE + data_size > block_len) {
            break;
        }
        dispatch_cmd(s, buf + offset + CHUNK_HDR_SIZE, data_size, resp);
        offset += CHUNK_HDR_SIZE + data_size;
    }
    g_free(buf);

    /*
     * Write down-channel response header + data to the start of the same
     * block. esp_hostdata_hdr_t: [uint16_t block_sz][data...]
     */
    bool has_resp = resp->len > 0;
    if (has_resp) {
        /*
         * The response is written back into the same TRAX block, after the
         * 2-byte length header, so it must fit in TRACEMEM_BLK_SIZE - 2.
         * Clamp here (not at the per-command layer): responses accumulate
         * across every command in the swap, so a single fread cap is not
         * enough to keep the writeback in bounds.
         */
        uint16_t resp_len = MIN(resp->len, TRACEMEM_BLK_SIZE - 2);
        uint8_t hdr_buf[2];
        stw_le_p(hdr_buf, resp_len);
        cpu_physical_memory_write(blk_addr, hdr_buf, 2);
        cpu_physical_memory_write(blk_addr + 2, resp->data, resp_len);
        trace_esp32s3_app_trace_resp(resp_len, (uint32_t)blk_addr);
    }
    g_byte_array_free(resp, TRUE);

    regs->ctrl_reg = CTRL_HOST_CONNECT
                   | (has_resp ? CTRL_HOST_DATA : 0)
                   | (block_id << CTRL_BLOCK_ID_SHIFT);
}

/* ------------------------------------------------------------------ */
/* ERI read/write — plugged into CPUXtensaState.er_read/er_write       */
/* opaque is the per-core Esp32s3AppTraceCtx                           */
/* ------------------------------------------------------------------ */

uint64_t esp32s3_app_trace_er_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp32s3AppTraceCtx *ctx = opaque;
    Esp32s3AppTraceCoreRegs *regs = &ctx->regs;

    switch (addr) {
    case ERI_TRAX_DELAYCNT:
        /* always report connected */
        return regs->ctrl_reg | CTRL_HOST_CONNECT;
    case ERI_TRAX_TRIGGERPC:
        return regs->stat_reg;
    case ERI_PERFMON_PM1:
        return regs->crc_reg;
    default:
        return 0;
    }
}

void esp32s3_app_trace_er_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    Esp32s3AppTraceCtx *ctx = opaque;
    Esp32s3AppTraceState *s = ctx->app_trace;
    Esp32s3AppTraceCoreRegs *regs = &ctx->regs;

    switch (addr) {
    case ERI_TRAX_DELAYCNT:
        process_swap_end(s, regs, (uint32_t)val);
        break;
    case ERI_TRAX_TRIGGERPC:
        regs->stat_reg = (uint32_t)val;
        break;
    case ERI_PERFMON_PM1:
        regs->crc_reg = (uint32_t)val;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* QOM object                                                          */
/* ------------------------------------------------------------------ */

static void esp32s3_app_trace_reset(void *opaque);

void *esp32s3_app_trace_core_context(Esp32s3AppTraceState *s, unsigned core_id)
{
    assert(core_id < ESP32S3_APP_TRACE_MAX_CORES);
    return &s->ctx[core_id];
}

Esp32s3AppTraceState *esp32s3_app_trace_new(const char *mode,
                                           const char *base_dir, Error **errp)
{
    if (!mode || strcmp(mode, "file_io") != 0) {
        error_setg(errp, "app_trace: unsupported mode '%s' "
                   "(only 'file_io' is supported)", mode ? mode : "");
        return NULL;
    }

    Esp32s3AppTraceState *s =
        ESP32S3_APP_TRACE(object_new(TYPE_ESP32S3_APP_TRACE));
    s->base_dir = base_dir ? g_strdup(base_dir) : NULL;
    qemu_register_reset(esp32s3_app_trace_reset, s);
    return s;
}

static void close_file(gpointer value)
{
    fclose((FILE *)value);
}

static void esp32s3_app_trace_init(Object *obj)
{
    Esp32s3AppTraceState *s = ESP32S3_APP_TRACE(obj);

    /* Open files are closed automatically when removed from / on destroy. */
    s->file_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                        NULL, close_file);
    for (unsigned i = 0; i < ESP32S3_APP_TRACE_MAX_CORES; i++) {
        s->ctx[i].app_trace = s;
    }
}

static void esp32s3_app_trace_reset(void *opaque)
{
    Esp32s3AppTraceState *s = opaque;

    /*
     * Drop any files still open from the previous run (closes them) and
     * clear every core's TRAX register bank, so a guest reboot starts the
     * AppTrace handshake from a clean state.
     */
    g_hash_table_remove_all(s->file_map);
    s->next_token = 0;
    for (unsigned i = 0; i < ESP32S3_APP_TRACE_MAX_CORES; i++) {
        memset(&s->ctx[i].regs, 0, sizeof(s->ctx[i].regs));
    }
}

static void esp32s3_app_trace_finalize(Object *obj)
{
    Esp32s3AppTraceState *s = ESP32S3_APP_TRACE(obj);

    qemu_unregister_reset(esp32s3_app_trace_reset, s);
    g_hash_table_destroy(s->file_map);  /* closes any still-open files */
    g_free(s->base_dir);
}

static const TypeInfo esp32s3_app_trace_info = {
    .name = TYPE_ESP32S3_APP_TRACE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(Esp32s3AppTraceState),
    .instance_init = esp32s3_app_trace_init,
    .instance_finalize = esp32s3_app_trace_finalize,
};

static void esp32s3_app_trace_register_types(void)
{
    type_register_static(&esp32s3_app_trace_info);
}

type_init(esp32s3_app_trace_register_types)
