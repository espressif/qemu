/*
 * Bare-metal ESP32-C3 I2C test for the QEMU model.
 *
 * Two modes, built from this one file and run as two guests by ./run.sh:
 * the default drives the I2C controller through its registers, and
 * -DTEST_BITBANG instead bit-bangs the pads with plain GPIO, the way a
 * software I2C master does. See README.md for what each covers and why this
 * is not a qtest.
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

#define UART0        0x60000000u
#define I2C0         0x60013000u
#define GPIO0        0x60004000u
#define A_CTR        0x04
#define A_SR         0x08
#define A_FIFO_CONF  0x18
#define A_DATA       0x1c
#define A_INT_RAW    0x20
#define A_INT_CLR    0x24
#define A_CMD        0x58

#define CTR_MS_MODE      (1u << 4)
#define CTR_TRANS_START  (1u << 5)
#define CTR_CONF_UPGATE  (1u << 11)
#define INT_TRANS_COMPLETE (1u << 7)
#define INT_NACK           (1u << 10)

#define OP_WRITE  1
#define OP_STOP   2
#define OP_READ   3
#define OP_END    4
#define OP_RSTART 6

static void putc_(char c) { REG(UART0) = (uint32_t)(unsigned char)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex2(uint32_t v)
{
    const char *d = "0123456789abcdef";
    putc_(d[(v >> 4) & 0xf]);
    putc_(d[v & 0xf]);
}

static int fails;

#ifndef TEST_BITBANG

static void hex4(uint32_t v) { hex2(v >> 8); hex2(v); }

static uint32_t cmd(uint32_t op, uint32_t byte_num, uint32_t ack_en)
{
    return (byte_num & 0xff) | (ack_en << 8) | (op << 11);
}

static void begin(void)
{
    REG(I2C0 + A_INT_CLR) = 0xffffffffu;
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_CONF_UPGATE;
    REG(I2C0 + A_FIFO_CONF) = (1u << 12) | (1u << 13);
    REG(I2C0 + A_FIFO_CONF) = 0;
}

/* Exactly the command list ESP-IDF's i2c_master_probe() builds. */
static uint32_t probe(uint8_t addr)
{
    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 1, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    return REG(I2C0 + A_INT_RAW);
}

static int read_reg(uint8_t addr, uint8_t reg, uint32_t *raw)
{
    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_DATA) = reg;
    REG(I2C0 + A_DATA) = ((uint32_t)addr << 1) | 1;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 2, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 12) = cmd(OP_WRITE, 1, 1);
    REG(I2C0 + A_CMD + 16) = cmd(OP_READ, 1, 0);
    REG(I2C0 + A_CMD + 20) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    *raw = REG(I2C0 + A_INT_RAW);
    if (((REG(I2C0 + A_SR) >> 8) & 0x3f) == 0) {
        return -1;
    }
    return (int)(REG(I2C0 + A_DATA) & 0xff);
}

static void write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_DATA) = reg;
    REG(I2C0 + A_DATA) = val;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 3, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
}

static void check_probe(uint8_t addr, int expect_ack)
{
    uint32_t raw = probe(addr);
    int ack = (raw & INT_TRANS_COMPLETE) && !(raw & INT_NACK);
    puts_("probe 0x"); hex2(addr);
    puts_(" INT_RAW=0x"); hex4(raw);
    puts_(ack ? " ACK " : " NACK");
    if (ack == expect_ack) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

static void check_read(uint8_t addr, uint8_t reg, int expect)
{
    uint32_t raw;
    int v = read_reg(addr, reg, &raw);
    puts_("read 0x"); hex2(addr); puts_("["); hex2(reg);
    puts_("] INT_RAW=0x"); hex4(raw); puts_(" val=");
    if (v < 0) {
        puts_("none");
    } else {
        puts_("0x"); hex2((uint32_t)v);
    }
    puts_(" expect=0x"); hex2((uint32_t)expect);
    if (v == expect) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

/*
 * Split a register read across an END, the way the driver does for transfers
 * that do not fit the FIFO. BUS_BUSY must read 0 in the gap (the driver spins
 * on it with no timeout) while the transfer itself stays open, so the
 * resumed command list must not re-interpret its first byte as an address.
 */
static void check_end_split(uint8_t addr, uint8_t reg, int expect)
{
    uint32_t raw1, busy1, raw2;
    int v;

    begin();
    REG(I2C0 + A_DATA) = (uint32_t)addr << 1;
    REG(I2C0 + A_DATA) = reg;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 2, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_END, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    raw1 = REG(I2C0 + A_INT_RAW);
    busy1 = (REG(I2C0 + A_SR) >> 4) & 1;

    /* Resume: no FIFO/INT reset, exactly as the driver refills the regs */
    REG(I2C0 + A_INT_CLR) = 0xffffffffu;
    REG(I2C0 + A_DATA) = ((uint32_t)addr << 1) | 1;
    REG(I2C0 + A_CMD + 0) = cmd(OP_RSTART, 0, 0);
    REG(I2C0 + A_CMD + 4) = cmd(OP_WRITE, 1, 1);
    REG(I2C0 + A_CMD + 8) = cmd(OP_READ, 1, 0);
    REG(I2C0 + A_CMD + 12) = cmd(OP_STOP, 0, 0);
    REG(I2C0 + A_CTR) = CTR_MS_MODE | CTR_TRANS_START;
    raw2 = REG(I2C0 + A_INT_RAW);
    v = (((REG(I2C0 + A_SR) >> 8) & 0x3f) == 0) ? -1 : (int)(REG(I2C0 + A_DATA) & 0xff);

    puts_("end-split 0x"); hex2(addr); puts_("["); hex2(reg);
    puts_("] INT_RAW=0x"); hex4(raw1); puts_(" busy=");
    putc_(busy1 ? '1' : '0');
    puts_(" then INT_RAW=0x"); hex4(raw2); puts_(" val=");
    if (v < 0) {
        puts_("none");
    } else {
        puts_("0x"); hex2((uint32_t)v);
    }
    if ((raw1 & (1u << 3)) && busy1 == 0 && v == expect) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

/* SCL_SP_CONF.SCL_RST_SLV_EN is R/W/SC: it must read back as 0. */
static void check_clr_bus(void)
{
    uint32_t v;

    REG(I2C0 + 0x80) = (5u << 1) | 1u;
    v = REG(I2C0 + 0x80);
    puts_("clr-bus SCL_SP_CONF=0x"); hex4(v);
    if ((v & 1u) == 0 && ((v >> 1) & 0x1f) == 5) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

#else /* TEST_BITBANG */

/*
 * A software I2C master, toggling two pads with plain GPIO writes and never
 * touching the I2C controller. This is what MicroPython's SoftI2C does, and
 * what oro's board autodetection probes the bus with before it knows which
 * board it is on.
 *
 * Open-drain is expressed the way the GPIO model understands it: clearing the
 * OUT bit drives the pad low, setting it releases the pad. A released pad
 * reads back high unless something on the bus is pulling it down, which is
 * how a slave's ACK arrives. No delays anywhere -- the emulated bus is
 * edge-driven, not timed.
 */
#define A_GPIO_OUT_W1TS     0x08
#define A_GPIO_OUT_W1TC     0x0c
#define A_GPIO_ENABLE_W1TS  0x24
#define A_GPIO_IN           0x3c

static void bb_set(int pad, int level)
{
    if (level) {
        REG(GPIO0 + A_GPIO_OUT_W1TS) = 1u << pad;
    } else {
        REG(GPIO0 + A_GPIO_OUT_W1TC) = 1u << pad;
    }
}

static int bb_get(int pad)
{
    return (int)((REG(GPIO0 + A_GPIO_IN) >> pad) & 1u);
}

/* The pad pair in use, so a test can bit-bang somewhere the bus is not. */
static int bb_scl, bb_sda;

static void bb_pads(int scl, int sda)
{
    bb_scl = scl;
    bb_sda = sda;
    /* Output enable is stored but not consulted by the model; set it anyway. */
    REG(GPIO0 + A_GPIO_ENABLE_W1TS) = (1u << scl) | (1u << sda);
    bb_set(sda, 1);
    bb_set(scl, 1);
}

static void bb_start(void)
{
    bb_set(bb_sda, 1);
    bb_set(bb_scl, 1);
    bb_set(bb_sda, 0);  /* SDA falls while SCL is high */
}

static void bb_stop(void)
{
    bb_set(bb_scl, 0);
    bb_set(bb_sda, 0);
    bb_set(bb_scl, 1);
    bb_set(bb_sda, 1);  /* SDA rises while SCL is high */
}

/* Returns 1 if the slave ACKed. */
static int bb_write_byte(uint8_t b)
{
    int ack;

    for (int i = 7; i >= 0; i--) {
        bb_set(bb_scl, 0);
        bb_set(bb_sda, (b >> i) & 1);
        bb_set(bb_scl, 1);
    }
    /* Release SDA and sample it on the ninth clock: low means ACK. */
    bb_set(bb_scl, 0);
    bb_set(bb_sda, 1);
    bb_set(bb_scl, 1);
    ack = !bb_get(bb_sda);
    bb_set(bb_scl, 0);
    return ack;
}

static uint8_t bb_read_byte(int ack)
{
    uint8_t v = 0;

    for (int i = 0; i < 8; i++) {
        bb_set(bb_scl, 0);
        bb_set(bb_sda, 1);  /* the slave drives, so stay off the line */
        bb_set(bb_scl, 1);
        v = (uint8_t)((v << 1) | bb_get(bb_sda));
    }
    bb_set(bb_scl, 0);
    bb_set(bb_sda, ack ? 0 : 1);
    bb_set(bb_scl, 1);
    bb_set(bb_scl, 0);
    return v;
}

static void check_bb_probe(int scl, int sda, uint8_t addr, int expect_ack)
{
    int ack;

    bb_pads(scl, sda);
    bb_start();
    ack = bb_write_byte((uint8_t)(addr << 1));
    bb_stop();

    puts_("bitbang pads "); hex2((uint32_t)scl); putc_('/');
    hex2((uint32_t)sda);
    puts_(" probe 0x"); hex2(addr);
    puts_(ack ? " ACK " : " NACK");
    if (ack == expect_ack) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

static void check_bb_read(int scl, int sda, uint8_t addr, uint8_t reg,
                          int expect)
{
    int ok;
    uint8_t v;

    bb_pads(scl, sda);
    bb_start();
    ok = bb_write_byte((uint8_t)(addr << 1));
    ok = ok && bb_write_byte(reg);
    /* Repeated start, then read one byte and NACK it to end the read. */
    bb_start();
    ok = ok && bb_write_byte((uint8_t)((addr << 1) | 1));
    v = bb_read_byte(0);
    bb_stop();

    puts_("bitbang read 0x"); hex2(addr); puts_("["); hex2(reg);
    puts_("] val=");
    if (!ok) {
        puts_("nack");
    } else {
        puts_("0x"); hex2(v);
    }
    puts_(" expect=0x"); hex2((uint32_t)expect);
    if (ok && v == (uint8_t)expect) {
        puts_(" ok\r\n");
    } else {
        puts_(" FAIL\r\n");
        fails++;
    }
}

#endif /* TEST_BITBANG */

void test_main(void)
{
    puts_("I2CTEST START\r\n");

#ifndef TEST_BITBANG
    check_probe(0x58, 1);
    check_probe(0x3a, 1);
    check_probe(0x08, 0);
    check_probe(0x5b, 0);
    /* A NACK must not wedge the controller: the next probe still works. */
    check_probe(0x58, 1);

    check_read(0x58, 0x10, 0x23);
    check_read(0x3a, 0x00, 0x18);
    check_read(0x58, 0x00, 0x00);

    write_reg(0x58, 0x02, 0x5a);
    write_reg(0x58, 0x03, 0xa5);
    check_read(0x58, 0x02, 0x5a);
    check_read(0x58, 0x03, 0xa5);
    /* The auto-incremented pointer must not leak into the next transfer */
    check_read(0x58, 0x10, 0x23);

    check_end_split(0x58, 0x10, 0x23);
    check_clr_bus();
    /* The controller must still be usable after both of those */
    check_probe(0x58, 1);
    check_read(0x3a, 0x00, 0x18);
#else
    /*
     * Run with -machine esp32c3,i2c-pads=7/6, so 7/6 is where the simulated
     * board's bus is and the bridge is bound there.
     */
    check_bb_probe(7, 6, 0x58, 1);
    check_bb_probe(7, 6, 0x3a, 1);
    check_bb_probe(7, 6, 0x08, 0);
    /* A NACK must leave the bus usable */
    check_bb_probe(7, 6, 0x58, 1);

    check_bb_read(7, 6, 0x58, 0x10, 0x23);
    check_bb_read(7, 6, 0x3a, 0x00, 0x18);

    /*
     * The case that makes board autodetection testable: bit-banging pads the
     * board's bus is NOT wired to must find nothing, even though the chips are
     * right there on the controller's bus. This is a Zero being probed on the
     * ZeroC/One pair, and it is the dead-bus fallback the firmware relies on.
     */
    check_bb_probe(1, 2, 0x58, 0);
    check_bb_probe(1, 2, 0x3a, 0);
    /* ...and the real bus still works afterwards */
    check_bb_probe(7, 6, 0x58, 1);
#endif

    puts_(fails ? "I2CTEST FAILURES\r\n" : "I2CTEST ALL OK\r\n");
    for (;;) {
    }
}
