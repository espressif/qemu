/*
 * ESP32-C3 I2C controller emulation
 *
 * Copyright (c) 2026 Oroblanco Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Modelled on hw/i2c/esp32_i2c.c. The register layout is nearly identical to
 * the original ESP32's, the notable differences being the number of command
 * registers, the command opcode values and the extra CTR bits.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/i2c/esp32c3_i2c.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"

static void esp32c3_i2c_do_transaction(Esp32C3I2CState *s);
static void esp32c3_i2c_update_irq(Esp32C3I2CState *s);

static void esp32c3_i2c_reset_hold(Object *obj, ResetType type)
{
    Esp32C3I2CState *s = ESP32C3_I2C(obj);

    fifo8_reset(&s->rx_fifo);
    fifo8_reset(&s->tx_fifo);
    s->transfer_open = false;
    s->bus_busy = false;
    s->ctr_reg = 0;
    s->fifo_conf_reg = 0;
    s->timeout_reg = 0;
    s->slave_addr_reg = 0;
    s->int_ena_reg = 0;
    s->int_raw_reg = 0;
    s->sda_hold_reg = 0;
    s->sda_sample_reg = 0;
    s->high_period_reg = 0;
    s->low_period_reg = 0;
    s->start_hold_reg = 0;
    s->rstart_setup_reg = 0;
    s->stop_hold_reg = 0;
    s->stop_setup_reg = 0;
    s->filter_cfg_reg = 0;
    s->clk_conf_reg = 0;
    s->scl_st_timeout_reg = 0;
    s->scl_main_st_timeout_reg = 0;
    s->scl_sp_conf_reg = 0;
    s->date_reg = 0x20070201;
    memset(s->cmd_reg, 0, sizeof(s->cmd_reg));
}

static uint32_t esp32c3_i2c_get_status_reg(Esp32C3I2CState *s)
{
    uint32_t res = 0;
    res = FIELD_DP32(res, I2C_STATUS, BUS_BUSY, s->bus_busy);
    res = FIELD_DP32(res, I2C_STATUS, RXFIFO_CNT, fifo8_num_used(&s->rx_fifo));
    res = FIELD_DP32(res, I2C_STATUS, TXFIFO_CNT, fifo8_num_used(&s->tx_fifo));
    return res;
}

static void esp32c3_i2c_update_irq(Esp32C3I2CState *s)
{
    int irq_state = !!(s->int_raw_reg & s->int_ena_reg);
    qemu_set_irq(s->irq, irq_state);
}

static uint64_t esp32c3_i2c_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32C3I2CState *s = ESP32C3_I2C(opaque);

    switch (addr) {
    case A_I2C_CTR:
        return s->ctr_reg;
    case A_I2C_STATUS:
        return esp32c3_i2c_get_status_reg(s);
    case A_I2C_FIFO_CONF:
        /* Read back, since the driver read-modify-writes the FIFO resets */
        return s->fifo_conf_reg;
    case A_I2C_FIFO_DATA: {
        if (fifo8_num_used(&s->rx_fifo) == 0) {
            error_report("esp32c3_i2c: read I2C FIFO while it is empty");
            return 0xee;
        }
        return fifo8_pop(&s->rx_fifo);
    }
    case A_I2C_INT_RAW:
        return s->int_raw_reg;
    case A_I2C_INT_ENA:
        return s->int_ena_reg;
    case A_I2C_INT_ST:
        return s->int_raw_reg & s->int_ena_reg;
    case A_I2C_CMD ... (A_I2C_CMD + (ESP32C3_I2C_CMD_COUNT - 1) * 4):
        return s->cmd_reg[(addr - A_I2C_CMD) / 4];
    case A_I2C_TIMEOUT:
        return s->timeout_reg;
    case A_I2C_SLAVE_ADDR:
        return s->slave_addr_reg;
    case A_I2C_FIFO_ST:
        return 0;
    case A_I2C_SDA_HOLD:
        return s->sda_hold_reg;
    case A_I2C_SDA_SAMPLE:
        return s->sda_sample_reg;
    case A_I2C_HIGH_PERIOD:
        return s->high_period_reg;
    case A_I2C_LOW_PERIOD:
        return s->low_period_reg;
    case A_I2C_START_HOLD:
        return s->start_hold_reg;
    case A_I2C_RSTART_SETUP:
        return s->rstart_setup_reg;
    case A_I2C_STOP_HOLD:
        return s->stop_hold_reg;
    case A_I2C_STOP_SETUP:
        return s->stop_setup_reg;
    case A_I2C_FILTER_CFG:
        return s->filter_cfg_reg;
    case A_I2C_CLK_CONF:
        return s->clk_conf_reg;
    case A_I2C_SCL_ST_TIME_OUT:
        return s->scl_st_timeout_reg;
    case A_I2C_SCL_MAIN_ST_TIME_OUT:
        return s->scl_main_st_timeout_reg;
    case A_I2C_SCL_SP_CONF:
        return s->scl_sp_conf_reg;
    case A_I2C_DATE:
        return s->date_reg;
    default:
        qemu_log_mask(LOG_UNIMP, "esp32c3_i2c: unimplemented read 0x%03"
                      HWADDR_PRIx "\n", addr);
        return 0;
    }
}

static void esp32c3_i2c_write(void *opaque, hwaddr addr, uint64_t value, unsigned int size)
{
    Esp32C3I2CState *s = ESP32C3_I2C(opaque);

    switch (addr) {
    case A_I2C_CTR:
        if (FIELD_EX32(value, I2C_CTR, MS_MODE) != 1) {
            error_report("esp32c3_i2c: slave mode not implemented");
        }
        if (FIELD_EX32(value, I2C_CTR, TRANS_START)) {
            esp32c3_i2c_do_transaction(s);
        }
        /*
         * TRANS_START, FSM_RST and CONF_UPGATE are write-trigger bits which
         * read back as 0. Only TRANS_START starts a transaction; the IDF
         * driver sets CONF_UPGATE after every configuration change.
         */
        value &= ~(R_I2C_CTR_TRANS_START_MASK | R_I2C_CTR_FSM_RST_MASK |
                   R_I2C_CTR_CONF_UPGATE_MASK);
        s->ctr_reg = value;
        break;
    case A_I2C_FIFO_CONF:
        if (FIELD_EX32(value, I2C_FIFO_CONF, NONFIFO_EN)) {
            error_report("esp32c3_i2c: APB mode not implemented");
        }
        if (FIELD_EX32(value, I2C_FIFO_CONF, RX_FIFO_RST)) {
            fifo8_reset(&s->rx_fifo);
        }
        if (FIELD_EX32(value, I2C_FIFO_CONF, TX_FIFO_RST)) {
            fifo8_reset(&s->tx_fifo);
        }
        /* The two reset bits are write triggers, everything else persists */
        s->fifo_conf_reg = value & ~(R_I2C_FIFO_CONF_RX_FIFO_RST_MASK |
                                     R_I2C_FIFO_CONF_TX_FIFO_RST_MASK);
        break;
    case A_I2C_FIFO_DATA:
        if (fifo8_num_free(&s->tx_fifo) == 0) {
            error_report("esp32c3_i2c: write to I2C TX FIFO while it is full");
        } else {
            fifo8_push(&s->tx_fifo, value);
        }
        break;
    case A_I2C_INT_CLR:
        s->int_raw_reg &= ~value;
        esp32c3_i2c_update_irq(s);
        break;
    case A_I2C_INT_ENA:
        s->int_ena_reg = value;
        esp32c3_i2c_update_irq(s);
        break;
    case A_I2C_CMD ... (A_I2C_CMD + (ESP32C3_I2C_CMD_COUNT - 1) * 4):
        s->cmd_reg[(addr - A_I2C_CMD) / 4] = value;
        break;
    case A_I2C_TIMEOUT:
        s->timeout_reg = value;
        break;
    case A_I2C_SLAVE_ADDR:
        s->slave_addr_reg = value;
        break;
    case A_I2C_SDA_HOLD:
        s->sda_hold_reg = value;
        break;
    case A_I2C_SDA_SAMPLE:
        s->sda_sample_reg = value;
        break;
    case A_I2C_HIGH_PERIOD:
        s->high_period_reg = value;
        break;
    case A_I2C_LOW_PERIOD:
        s->low_period_reg = value;
        break;
    case A_I2C_START_HOLD:
        s->start_hold_reg = value;
        break;
    case A_I2C_RSTART_SETUP:
        s->rstart_setup_reg = value;
        break;
    case A_I2C_STOP_HOLD:
        s->stop_hold_reg = value;
        break;
    case A_I2C_STOP_SETUP:
        s->stop_setup_reg = value;
        break;
    case A_I2C_FILTER_CFG:
        s->filter_cfg_reg = value;
        break;
    case A_I2C_CLK_CONF:
        s->clk_conf_reg = value;
        break;
    case A_I2C_SCL_ST_TIME_OUT:
        s->scl_st_timeout_reg = value;
        break;
    case A_I2C_SCL_MAIN_ST_TIME_OUT:
        s->scl_main_st_timeout_reg = value;
        break;
    case A_I2C_SCL_SP_CONF:
        /*
         * SCL_RST_SLV_EN is "R/W/SC": the hardware clears it once the SCL
         * pulses have been sent. s_i2c_master_clear_bus() spins until it
         * reads back 0, and gives up with "clear bus failed." after 50ms --
         * after which every later transaction fails with "reset hardware
         * failed". Nothing to simulate, so retire it immediately.
         */
        s->scl_sp_conf_reg = value & ~R_I2C_SCL_SP_CONF_SCL_RST_SLV_EN_MASK;
        break;
    case A_I2C_DATE:
        s->date_reg = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "esp32c3_i2c: unimplemented write 0x%03"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n", addr, value);
        break;
    }
}

/*
 * Is `pad` free for I2C to use -- either driven by nothing in particular
 * (SIG_GPIO_OUT_IDX, the reset state and what the pad looks like before the
 * I2C driver claims it) or driven by I2C itself? A different peripheral's
 * output signal means contention.
 */
static bool esp32c3_i2c_pad_undriven(Esp32C3I2CState *s, int pad, unsigned i2c_sig)
{
    if (pad < 0) {
        return true;
    }
    int sig = esp32c3_gpio_pad_out_signal(s->gpio, pad);
    if (sig < 0 || sig == ESP32C3_SIG_GPIO_OUT_IDX || sig == (int)i2c_sig) {
        return true;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "esp32c3_i2c: pad %d is driven by signal %d, not I2C (%u) -- "
                  "bus unusable\n", pad, sig, i2c_sig);
    return false;
}

/*
 * Is the controller currently routed to the pads the simulated board's bus is
 * wired to? Fails open in two cases that both have to keep working: no
 * "i2c-pads" given at all, and a guest that has not routed I2C through the
 * GPIO matrix yet (or has torn the routing down again, which
 * i2c_common_deinit_pins() does on i2c_del_master_bus()). So the check only
 * bites while a bus handle is open, which is when probes happen.
 */
static bool esp32c3_i2c_pads_match(Esp32C3I2CState *s)
{
    if (s->board_scl < 0 && s->board_sda < 0) {
        return true;
    }
    if (s->gpio == NULL) {
        return true;
    }

    int scl = esp32c3_gpio_signal_pad(s->gpio, ESP32C3_I2CEXT0_SCL_IN_IDX);
    int sda = esp32c3_gpio_signal_pad(s->gpio, ESP32C3_I2CEXT0_SDA_IN_IDX);

    if (s->board_scl >= 0 && scl >= 0 && scl != s->board_scl) {
        return false;
    }
    if (s->board_sda >= 0 && sda >= 0 && sda != s->board_sda) {
        return false;
    }

    /*
     * Routed to us is not the same as usable: another peripheral can have
     * taken the pad on the OUT side while our in-sel entries still point at
     * it, so the checks above pass and the bus looks healthy.
     *
     * That is exactly what a stale board I2S pin map does.
     * i2s_channel_init_std_mode() points func_out_sel_cfg[pad] at an I2S
     * signal and never touches I2CEXT0_*_IN_SEL, so without this the guest
     * sees a working bus where real hardware sees a pad being actively driven
     * push-pull and an open-drain I2C line that can no longer be pulled --
     * i.e. every transaction timing out.
     */
    if (!esp32c3_i2c_pad_undriven(s, s->board_scl, ESP32C3_I2CEXT0_SCL_OUT_IDX) ||
        !esp32c3_i2c_pad_undriven(s, s->board_sda, ESP32C3_I2CEXT0_SDA_OUT_IDX)) {
        return false;
    }
    return true;
}

static void esp32c3_i2c_do_transaction(Esp32C3I2CState *s)
{
    bool stop_or_end = false;

    /* The command FSM is running; STOP, END and a NACK each make it idle */
    s->bus_busy = true;

    for (int i_cmd = 0; i_cmd < ESP32C3_I2C_CMD_COUNT && !stop_or_end; ++i_cmd) {
        uint32_t cmd = s->cmd_reg[i_cmd];
        uint32_t opcode = FIELD_EX32(cmd, I2C_CMD, OPCODE);

        switch (opcode) {
        case ESP32C3_I2C_OPCODE_RSTART:
            i2c_end_transfer(s->bus);
            s->transfer_open = false;
            break;
        case ESP32C3_I2C_OPCODE_WRITE: {
            size_t length = FIELD_EX32(cmd, I2C_CMD, BYTE_NUM);
            if (!s->transfer_open) {
                s->transfer_open = true;
                uint8_t data = fifo8_pop(&s->tx_fifo);
                uint8_t addr = data >> 1;
                uint8_t is_read = data & 0x1;
                /*
                 * Driving pads the board's bus is not wired to is
                 * indistinguishable from an empty bus, so skip it entirely
                 * rather than letting the slaves answer.
                 */
                bool nacked = !esp32c3_i2c_pads_match(s)
                              || i2c_start_transfer(s->bus, addr, is_read) != 0;
                if (nacked) {
                    /* Nothing answered the address phase: NACK */
                    if (FIELD_EX32(cmd, I2C_CMD, ACK_CHECK_EN)
                        && FIELD_EX32(cmd, I2C_CMD, ACK_EXP) == 0) {
                        s->int_raw_reg = FIELD_DP32(s->int_raw_reg, I2C_INT_RAW, NACK, 1);
                        stop_or_end = true;
                    }
                    s->transfer_open = false;
                    s->bus_busy = false;
                    break;
                }
                s->int_raw_reg = FIELD_DP32(s->int_raw_reg, I2C_INT_RAW, NACK, 0);
                length -= 1;
            }
            for (size_t nbytes = 0; nbytes < length; ++nbytes) {
                uint8_t data = fifo8_pop(&s->tx_fifo);
                i2c_send(s->bus, data);
            }
            break;
        }
        case ESP32C3_I2C_OPCODE_READ: {
            size_t length = FIELD_EX32(cmd, I2C_CMD, BYTE_NUM);
            for (size_t nbytes = 0; nbytes < length; ++nbytes) {
                if (fifo8_num_free(&s->rx_fifo) == 0) {
                    error_report("esp32c3_i2c: RX FIFO overflow");
                } else {
                    fifo8_push(&s->rx_fifo, i2c_recv(s->bus));
                }
            }
            break;
        }
        case ESP32C3_I2C_OPCODE_STOP:
            i2c_end_transfer(s->bus);
            s->transfer_open = false;
            s->bus_busy = false;
            s->int_raw_reg = FIELD_DP32(s->int_raw_reg, I2C_INT_RAW, TRANS_COMPLETE, 1);
            stop_or_end = true;
            break;
        case ESP32C3_I2C_OPCODE_END:
            /*
             * END pauses the command list without releasing the bus: the
             * transfer stays open and the driver refills the command
             * registers. The command FSM is idle in that gap though, so
             * BUS_BUSY must read back 0 -- the driver spins on it with no
             * timeout before writing the next batch of commands.
             */
            s->bus_busy = false;
            s->int_raw_reg = FIELD_DP32(s->int_raw_reg, I2C_INT_RAW, END_DETECT, 1);
            stop_or_end = true;
            break;
        default:
            error_report("esp32c3_i2c: Invalid command %d opcode %d", i_cmd, opcode);
            break;
        }
        s->cmd_reg[i_cmd] = FIELD_DP32(s->cmd_reg[i_cmd], I2C_CMD, DONE, 1);
    }

    if (!stop_or_end) {
        /*
         * Ran off the end of the command list without a STOP or END. Real
         * firmware always terminates the list, but leaving BUS_BUSY asserted
         * here would wedge the guest in an untimed spin.
         */
        s->bus_busy = false;
    }
    esp32c3_i2c_update_irq(s);
}

static const MemoryRegionOps esp32c3_i2c_ops = {
    .read = esp32c3_i2c_read,
    .write = esp32c3_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_i2c_init(Object *obj)
{
    Esp32C3I2CState *s = ESP32C3_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_i2c_ops, s, TYPE_ESP32C3_I2C,
                          ESP32C3_I2C_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->bus = i2c_init_bus(DEVICE(s), "i2c");

    fifo8_create(&s->tx_fifo, ESP32C3_I2C_FIFO_LENGTH);
    fifo8_create(&s->rx_fifo, ESP32C3_I2C_FIFO_LENGTH);

    object_property_add_link(obj, "gpio", TYPE_ESP32C3_GPIO,
                             (Object **)&s->gpio,
                             qdev_prop_allow_set_link_before_realize, 0);
}

static Property esp32c3_i2c_properties[] = {
    DEFINE_PROP_INT32("board-scl", Esp32C3I2CState, board_scl, -1),
    DEFINE_PROP_INT32("board-sda", Esp32C3I2CState, board_sda, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void esp32c3_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, esp32c3_i2c_properties);
    rc->phases.hold = esp32c3_i2c_reset_hold;
}

static const TypeInfo esp32c3_i2c_type_info = {
    .name = TYPE_ESP32C3_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32C3I2CState),
    .instance_init = esp32c3_i2c_init,
    .class_init = esp32c3_i2c_class_init,
};

static void esp32c3_i2c_register_types(void)
{
    type_register_static(&esp32c3_i2c_type_info);
}

type_init(esp32c3_i2c_register_types)
