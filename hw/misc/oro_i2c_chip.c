/*
 * Generic simulated I2C peripheral with a flat register file
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Covers the register protocol shared by the GPIO expanders, LED drivers and
 * touch controllers found on the Oroblanco boards: the first byte written
 * after a START selects a register index, subsequent writes store with
 * auto-increment, and reads return the register file with auto-increment.
 *
 * Instantiate one per simulated chip, e.g.
 *     -device 'oro-i2c-chip,address=0x58,name=aw9523b,regs=0x10:0x23;0x7f:0'
 *
 * Which pads the bus is wired to is a property of the board, not of each
 * chip, so it lives on the machine ("i2c-pads") and is enforced by the
 * controller.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/i2c/i2c.h"
#include "hw/misc/oro_i2c_chip.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

OBJECT_DECLARE_SIMPLE_TYPE(OroI2CChipState, ORO_I2C_CHIP)

#define ORO_I2C_CHIP_NREGS 256

/* Debug output */
#define ORO_I2C_CHIP_DEBUG 0

struct OroI2CChipState {
    I2CSlave parent_obj;

    /* Properties */
    char *name;
    char *regs_preset;

    uint8_t regs[ORO_I2C_CHIP_NREGS];
    uint8_t ptr;
    bool expect_reg;
};

static const char *oro_i2c_chip_name(OroI2CChipState *s)
{
    return s->name ? s->name : TYPE_ORO_I2C_CHIP;
}

static int oro_i2c_chip_event(I2CSlave *i2c, enum i2c_event event)
{
    OroI2CChipState *s = ORO_I2C_CHIP(i2c);

    switch (event) {
    case I2C_START_SEND:
        /* The first byte of a write selects the register to access */
        s->expect_reg = true;
        break;
    case I2C_START_RECV:
        /*
         * A read continues from the register selected by the preceding write,
         * so the pointer is deliberately left alone here. The same goes for
         * I2C_FINISH: the controller model ends the transfer on a repeated
         * start, and the pointer must survive that.
         */
        break;
    case I2C_FINISH:
    case I2C_NACK:
        break;
    default:
        return -1;
    }

    return 0;
}

static uint8_t oro_i2c_chip_recv(I2CSlave *i2c)
{
    OroI2CChipState *s = ORO_I2C_CHIP(i2c);
    uint8_t value = s->regs[s->ptr];

#if ORO_I2C_CHIP_DEBUG
    info_report("[%s @0x%02x] READ reg 0x%02x = 0x%02x",
                oro_i2c_chip_name(s), i2c->address, s->ptr, value);
#endif

    s->ptr++;
    return value;
}

static int oro_i2c_chip_send(I2CSlave *i2c, uint8_t data)
{
    OroI2CChipState *s = ORO_I2C_CHIP(i2c);

    if (s->expect_reg) {
        s->ptr = data;
        s->expect_reg = false;
#if ORO_I2C_CHIP_DEBUG
        info_report("[%s @0x%02x] SELECT reg 0x%02x",
                    oro_i2c_chip_name(s), i2c->address, s->ptr);
#endif
        return 0;
    }

#if ORO_I2C_CHIP_DEBUG
    info_report("[%s @0x%02x] WRITE reg 0x%02x = 0x%02x",
                oro_i2c_chip_name(s), i2c->address, s->ptr, data);
#endif

    s->regs[s->ptr] = data;
    s->ptr++;
    return 0;
}

/*
 * Parse one "regs" number. A "0x"/"0X" prefix means hexadecimal, anything
 * else decimal.
 *
 * Deliberately not base 0: that would also read a leading zero as octal, so
 * "010" would quietly mean 8. In a field where both "0x00" and "00" look
 * natural that is a trap, and it contradicts "no prefix means decimal".
 */
static int oro_i2c_chip_parse_num(const char *s, uint64_t *out)
{
    const char *body = s;
    int base = 10;

    if (body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
        body += 2;
        base = 16;
    }
    if (body[0] == '\0') {
        return -EINVAL;
    }
    return qemu_strtou64(body, NULL, base, out);
}

/*
 * Parse the "regs" property: a list of "<index>:<value>" pairs, e.g.
 * "0x10:0x23;0x00:0x18". Each number is hexadecimal with a "0x" prefix and
 * decimal without, so "0x10:0x23" and "16:35" set the same register to the
 * same value. Entries are separated by ';' because a comma is -device's own
 * option separator and never reaches this property.
 */
static void oro_i2c_chip_parse_regs(OroI2CChipState *s, Error **errp)
{
    g_auto(GStrv) entries = NULL;

    if (!s->regs_preset || s->regs_preset[0] == '\0') {
        return;
    }

    entries = g_strsplit(s->regs_preset, ";", -1);
    for (int i = 0; entries[i] != NULL; i++) {
        g_auto(GStrv) pair = g_strsplit(entries[i], ":", -1);
        uint64_t index;
        uint64_t value;

        if (g_strv_length(pair) != 2) {
            error_setg(errp, "%s: malformed regs entry '%s', expected "
                       "<index>:<value> entries separated by ';' "
                       "(0x prefix for hex, otherwise decimal)",
                       oro_i2c_chip_name(s), entries[i]);
            return;
        }
        if (oro_i2c_chip_parse_num(g_strstrip(pair[0]), &index) < 0 ||
            index >= ORO_I2C_CHIP_NREGS) {
            error_setg(errp, "%s: invalid regs index '%s', expected 0..0xff",
                       oro_i2c_chip_name(s), pair[0]);
            return;
        }
        if (oro_i2c_chip_parse_num(g_strstrip(pair[1]), &value) < 0 ||
            value > 0xff) {
            error_setg(errp, "%s: invalid regs value '%s', expected 0..0xff",
                       oro_i2c_chip_name(s), pair[1]);
            return;
        }

        s->regs[index] = value;
    }
}

static void oro_i2c_chip_realize(DeviceState *dev, Error **errp)
{
    OroI2CChipState *s = ORO_I2C_CHIP(dev);

    oro_i2c_chip_parse_regs(s, errp);
}

static void oro_i2c_chip_reset_hold(Object *obj, ResetType type)
{
    OroI2CChipState *s = ORO_I2C_CHIP(obj);

    s->ptr = 0;
    s->expect_reg = false;
}

static const VMStateDescription vmstate_oro_i2c_chip = {
    .name = TYPE_ORO_I2C_CHIP,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, OroI2CChipState),
        VMSTATE_UINT8_ARRAY(regs, OroI2CChipState, ORO_I2C_CHIP_NREGS),
        VMSTATE_UINT8(ptr, OroI2CChipState),
        VMSTATE_BOOL(expect_reg, OroI2CChipState),
        VMSTATE_END_OF_LIST()
    }
};

static Property oro_i2c_chip_properties[] = {
    /* The "address" property comes from TYPE_I2C_SLAVE */
    DEFINE_PROP_STRING("name", OroI2CChipState, name),
    DEFINE_PROP_STRING("regs", OroI2CChipState, regs_preset),
    DEFINE_PROP_END_OF_LIST()
};

static void oro_i2c_chip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->realize = oro_i2c_chip_realize;
    dc->vmsd = &vmstate_oro_i2c_chip;
    device_class_set_props(dc, oro_i2c_chip_properties);
    rc->phases.hold = oro_i2c_chip_reset_hold;

    sc->event = oro_i2c_chip_event;
    sc->recv = oro_i2c_chip_recv;
    sc->send = oro_i2c_chip_send;
}

static const TypeInfo oro_i2c_chip_info = {
    .name = TYPE_ORO_I2C_CHIP,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(OroI2CChipState),
    .class_init = oro_i2c_chip_class_init,
};

static void oro_i2c_chip_register_types(void)
{
    type_register_static(&oro_i2c_chip_info);
}

type_init(oro_i2c_chip_register_types)
