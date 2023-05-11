/*
 * ESP32-C3 GDMA emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/dma.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/dma/esp32c3_gdma.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/error-report.h"

#define GDMA_WARNING 0
#define GDMA_DEBUG   0


/**
 * @brief Structure defining how linked lists are represented in hardware for the GDMA module
 */
typedef struct GdmaLinkedList {
    union {
        struct {
            uint32_t size: 12;   // Size of the buffer (mainly used in a receive transaction)
            uint32_t length: 12; // Number of valid bytes in the buffer. In a transmit, written by software.
                                 // In receive, written by hardware.
            uint32_t rsvd_24: 4; // Reserved
            uint32_t err_eof: 1; // Set if received data has errors. Used with UHCI0 only.
            uint32_t rsvd_29: 1; // Reserved
            uint32_t suc_eof: 1; // Set if curent node is the last one (of the list). Set by software in a transmit transaction,
                                 // Set by the hardware in case of a receive transaction.
            uint32_t owner: 1;   // 0: CPU can access the buffer, 1: GDMA can access the buffer. Cleared automatically
                                 // by hardware in a transmit descriptor. In a receive descriptor, cleared by hardware
                                 // only if GDMA_OUT_AUTO_WRBACK_CHn is set to 1.
        };
        uint32_t val;
    } config;
    uint32_t buf_addr;
    uint32_t next_addr;
} GdmaLinkedList;


static uint32_t read_addr_word(uint32_t* ptr, uint32_t offset) {
    return ptr[offset / sizeof(uint32_t)];
}


static uint64_t esp32c3_gdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(opaque);
    uint64_t r = 0;


    switch(addr) {
        case A_DMA_INT_RAW_CH0 ... A_DMA_INT_CLR_CH2:
            r = read_addr_word((uint32_t*) s->ch_int, addr - A_DMA_INT_RAW_CH0);
            break;

        case A_DMA_MISC_CONF:
            r = s->misc_conf;
            break;

        case A_DMA_IN_CONF0_CH0 ... A_DMA_IN_PERI_SEL_CH0:
            r = read_addr_word((uint32_t*) &s->ch_conf[0][ESP32C3_GDMA_IN_IDX], addr - A_DMA_IN_CONF0_CH0);
            break;

        case A_DMA_OUT_CONF0_CH0 ... A_DMA_OUT_PERI_SEL_CH0:
            r = read_addr_word((uint32_t*) &s->ch_conf[0][ESP32C3_GDMA_OUT_IDX], addr - A_DMA_OUT_CONF0_CH0);
            break;

        case A_DMA_IN_CONF0_CH1 ... A_DMA_IN_PERI_SEL_CH1:
            r = read_addr_word((uint32_t*) &s->ch_conf[1][ESP32C3_GDMA_IN_IDX], addr - A_DMA_IN_CONF0_CH1);
            break;

        case A_DMA_OUT_CONF0_CH1 ... A_DMA_OUT_PERI_SEL_CH1:
            r = read_addr_word((uint32_t*) &s->ch_conf[1][ESP32C3_GDMA_OUT_IDX], addr - A_DMA_OUT_CONF0_CH1);
            break;

        case A_DMA_IN_CONF0_CH2 ... A_DMA_IN_PERI_SEL_CH2:
            r = read_addr_word((uint32_t*) &s->ch_conf[2][ESP32C3_GDMA_IN_IDX], addr - A_DMA_IN_CONF0_CH2);
            break;

        case A_DMA_OUT_CONF0_CH2 ... A_DMA_OUT_PERI_SEL_CH2:
            r = read_addr_word((uint32_t*) &s->ch_conf[2][ESP32C3_GDMA_OUT_IDX], addr - A_DMA_OUT_CONF0_CH2);
            break;

        default:
#if GDMA_WARNING
            warn_report("[GDMA] Unsupported read to %08lx", addr);
#endif
            break;
    }

#if GDMA_DEBUG
    info_report("[GDMA] Reading from %08lx (%08lx)", addr, r);
#endif

    return r;
}


/**
 * @brief Check whether the new status of any interrupt should trigger an interrupt
 *
 * @param s GDMA state structure
 * @param chan Channel that has just been updated
 */
static void esp32c3_gdma_check_interrupt_status(ESP32C3GdmaState *s, uint32_t chan)
{
    DmaIntState* const int_st = &s->ch_int[chan];
    const uint32_t former = int_st->st;

    /* Calculate the new status and check for any difference */
    int_st->st = int_st->raw & int_st->ena;

    if (former != int_st->st) {
        /* If all the status bits became low, lower the IRQ pin, else, raise it  */
        qemu_set_irq(s->irq[chan], int_st->st ? 1 : 0);
    }
}


/**
 * @brief Set the status bit for the given channel. If the status triggers an interrupt, the corresponding
 * IRQ will be set.
*/
static void esp32c3_gdma_set_status(ESP32C3GdmaState *s, uint32_t chan, uint32_t mask)
{
    s->ch_int[chan].raw |= mask;
    esp32c3_gdma_check_interrupt_status(s, chan);
}



/**
 * @brief Clear the status bit for the given channel
*/
static void esp32c3_gdma_clear_status(ESP32C3GdmaState *s, uint32_t chan, uint32_t mask)
{
    s->ch_int[chan].raw &= ~mask;
    esp32c3_gdma_check_interrupt_status(s, chan);
}


/**
 * @brief Function called when a write to a channel interrupt register is performed
 *
 * @param s GDMA state structure
 * @param chan Index of the channel to be written
 * @param reg Offset, in bytes, of the register to modify
 * @param value New value for the register
 */
static void esp32c3_gdma_write_int_state(ESP32C3GdmaState *s, uint32_t chan, uint32_t reg, uint32_t value)
{
    switch (reg) {
        case offsetof(DmaIntState, ena):
            s->ch_int[chan].ena = value;
            break;

        case offsetof(DmaIntState, raw):
        case offsetof(DmaIntState, clr):
            /* Clear the bits that are set to 1, keep the remaining to their original value */
            s->ch_int[chan].raw &= ~value;
            break;

        case offsetof(DmaIntState, st):
        default:
            /* Nothing to do, read-only register, return directly */
            return;
    }

    /* Update the status and check if any interrupt needs to occur */
    esp32c3_gdma_check_interrupt_status(s, chan);
}


/**
 * @brief Function called when a reset FIFO is requested
 *
 * @param s GDMA state structure
 * @param chan Index of the channel
 * @param in_out Index of the direction, ESP32C3_GDMA_IN_IDX or ESP32C3_GDMA_OUT_IDX,
 *        that needs a FIFO reset
 */
static void esp32c3_gdma_reset_fifo(ESP32C3GdmaState *s, uint32_t chan, uint32_t in_out)
{
#if GDMA_DEBUG
    info_report("Resetting FIFO for chan %d, direction: %d", chan, in_out);
#endif
    /* Set the FIFO empty bit to 1, full bit to 0, and number of bytes of data to 0 */
    s->ch_conf[chan][in_out].status = R_DMA_INFIFO_STATUS_CH0_INFIFO_EMPTY_CH0_MASK;
}


/**
 * @brief Read a descriptor from the guest machine
 *
 * @param s GDMA state structure
 * @param addr Guest machine address
 *
 * @returns true if the transfer was a success, false else
 */
static bool esp32c3_gdma_read_descr(ESP32C3GdmaState *s, uint32_t addr, GdmaLinkedList* out)
{
    MemTxResult res = dma_memory_read(&s->dma_as, addr, out, sizeof(GdmaLinkedList), MEMTXATTRS_UNSPECIFIED);
    return res == MEMTX_OK;
}

/**
 * @brief Write a descriptor to the guest machine
 *
 * @param s GDMA state structure
 * @param addr Guest machine address
 *
 * @returns true if the transfer was a success, false else
 */
static bool esp32c3_gdma_write_descr(ESP32C3GdmaState *s, uint32_t addr, GdmaLinkedList* in)
{
    MemTxResult res = dma_memory_write(&s->dma_as, addr, in, sizeof(GdmaLinkedList), MEMTXATTRS_UNSPECIFIED);
    return res == MEMTX_OK;
}



/**
 * @brief Read and write arbitrary data from and to the guest machine
 *
 * @param s GDMA state structure
 * @param addr Guest machine address
 *
 * @returns true if the transfer was a success, false else
 */
static bool esp32c3_gdma_read_guest(ESP32C3GdmaState *s, uint32_t addr, void* data, uint32_t len)
{
    MemTxResult res = dma_memory_read(&s->dma_as, addr, data, len, MEMTXATTRS_UNSPECIFIED);
    return res == MEMTX_OK;
}

static bool esp32c3_gdma_write_guest(ESP32C3GdmaState *s, uint32_t addr, void* data, uint32_t len)
{
    MemTxResult res = dma_memory_write(&s->dma_as, addr, data, len, MEMTXATTRS_UNSPECIFIED);
    return res == MEMTX_OK;
}


/**
 * @brief Push current node (guest) address in the list of descriptors registers
 *
 * @param s GDMA state structure
 * @param chan Channel to update
 * @param chan Direction to update
 * @param current New node (guest) address to set as the current
 */
static void esp32c3_gdma_push_descriptor(ESP32C3GdmaState *s, uint32_t chan, uint32_t dir, uint32_t current)
{
    GdmaLinkedList next_node;
    uint32_t next = 0;
    DmaConfigState* state = &s->ch_conf[chan][dir];

    /* Assign the current descriptor address to the state register */
    state->state = current & R_DMA_OUT_STATE_CH0_OUTLINK_DSCR_ADDR_CH0_MASK;

    /* On real hardware, if the former address is incorrect, the current address is copied to this
     * register. */
    state->bfr_bfr_desc_addr = state->bfr_desc_addr;
    /* On real hardware, state->bfr_desc_addr is taken from state->desc_addr, even is `current` is valid */
    state->bfr_desc_addr = state->desc_addr;

    /* Get the next address out of the guest RAM */
    const bool valid = esp32c3_gdma_read_descr(s, current, &next_node);
    if (valid) {
        next = next_node.next_addr;
    }
    state->desc_addr = next;
}



/**
 * @brief Jump to the next node list and assign it to the given node
 *
 * @param s GDMA state structure
 * @param node Node to get the next neighbor of
 *
 * @returns true if the next node is valid, false else
 */
static inline bool esp32c3_gdma_next_list_node(ESP32C3GdmaState *s, uint32_t chan, uint32_t dir, GdmaLinkedList* node)
{
    const uint32_t current = node->next_addr;
    esp32c3_gdma_push_descriptor(s, chan, dir, current);
    return esp32c3_gdma_read_descr(s, current, node);
}


/**
 * @brief Get the first descriptor to process when a restart is requested.
 * We need to get the "next" node of the last one processed, which is in `desc_addr` register
 *
 * @param s GDMA state structure
 * @param chan Channel to restart
 * @param dir Direction (INT or OUT) to restart
 * @param out Filled with the output guest address
 */
static void esp32c3_gdma_get_restart_buffer(ESP32C3GdmaState *s, uint32_t chan, uint32_t dir, uint32_t* out)
{
    DmaConfigState* state = &s->ch_conf[chan][dir];
    // GdmaLinkedList* list = NULL;
    /* The next node to use is taken from state->state's lowest 18 bit. Append it to the DRAM address */
    const uint32_t dram_upper_bits = ESP32C3_GDMA_RAM_ADDR & (~R_DMA_OUT_STATE_CH0_OUTLINK_DSCR_ADDR_CH0_MASK);
    const uint32_t guest_addr = dram_upper_bits | FIELD_EX32(state->state, DMA_OUT_STATE_CH0, OUTLINK_DSCR_ADDR_CH0);

    *out = guest_addr;
}


/**
 * Check the header file for more info about this function
 */
bool esp32c3_gdma_get_channel_periph(ESP32C3GdmaState *s, GdmaPeripheral periph, int dir, uint32_t* chan)
{
    /* If the state, the peripheral or the direction is invalid, return directly */
    if (s == NULL || chan == NULL ||
        periph > GDMA_LAST || periph == GDMA_RSVD1 || periph == GDMA_RSVD4 || periph == GDMA_RSVD5 ||
        dir < 0 || dir >= ESP32C3_GDMA_CONF_COUNT)
    {
        return false;
    }

    /* Check all the channels of the GDMA */
    for (int i = 0; i < ESP32C3_GDMA_CHANNEL_COUNT; i++) {
        /* IN/OUT PERI registers have the same organization, can use any macro.
         * Look for the channel that was configured with the given peripheral. It must be marked as "started" too */
        if ( FIELD_EX32(s->ch_conf[i][dir].peripheral, DMA_IN_PERI_SEL_CH0, PERI_IN_SEL_CH0) == periph ||
             FIELD_EX32(s->ch_conf[i][dir].link, DMA_OUT_LINK_CH0, OUTLINK_START_CH0)) {

            *chan = i;
            return true;
        }
    }

    return false;
}


/**
 * @brief Read data from guest RAM pointed by the linked list configured in the given DmaConfigState index.
 *        `size` bytes will be read and stored in `buffer`.
 */
bool esp32c3_gdma_read_channel(ESP32C3GdmaState *s, uint32_t chan, uint8_t* buffer, uint32_t size)
{
    DmaConfigState* state = &s->ch_conf[chan][ESP32C3_GDMA_OUT_IDX];

    state->link &= R_DMA_OUT_LINK_CH0_OUTLINK_ADDR_CH0_MASK;

    /* Same goes for the status */
    esp32c3_gdma_clear_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DONE_CH0_INT_RAW_MASK |
                                       R_DMA_INT_RAW_CH0_OUT_EOF_CH0_INT_RAW_MASK);

    /* Get the guest DRAM address */
    uint32_t out_addr = ((ESP32C3_GDMA_RAM_ADDR >> 20) << 20) | FIELD_EX32(state->link, DMA_OUT_LINK_CH0, OUTLINK_ADDR_CH0);

    /* Boolean to mark whether we need to check the owner for in and out buffers */
    const bool owner_check_out = FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].conf1, DMA_OUT_CONF1_CH0, OUT_CHECK_OWNER_CH0);

    /* Boolean to mark whether the transmit (out) buffers must have their owner bit cleared here */
    const bool clear_out = FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].conf0, DMA_OUT_CONF0_CH0, OUT_AUTO_WRBACK_CH0);

    /* Pointer to the lists that will be browsed by the loop below */
    GdmaLinkedList out_list;

    /* Boolean to mark whether a descriptor error occurred during the transfer */
    bool valid = true;

    /* Set the current buffer (guest address) in the `desc_addr` register */
    valid = esp32c3_gdma_read_descr(s, out_addr, &out_list);
    esp32c3_gdma_push_descriptor(s, chan, ESP32C3_GDMA_OUT_IDX, out_addr);

    /* Check that the address is valid. If the owner must be checked, make sure owner is the DMA controller.
     * On the real hardware, both in and out are checked at the same time, so in case of an error, both bits
     * are set. Replicate the same behavior here. */
    if ( !valid || (owner_check_out && !out_list.config.owner) ) {
        esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
        return false;
    }

    /* Store the current number of bytes written to `buffer` parameter */
    uint32_t consumed = 0;
    bool exit_loop = false;
    bool error = false;

    while (!exit_loop && !error) {
        /* Calculate the number of bytes to read from the OUT channel */
        const uint32_t remaining = size - consumed;
        const uint32_t min = MIN(out_list.config.length, remaining);

        valid = esp32c3_gdma_read_guest(s, out_list.buf_addr, buffer + consumed, min);
        if (!valid) {
            esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
            error = true;
            break;
        }
        consumed += min;

        if (consumed == size) {
            exit_loop = true;
        }

        /* If we reached the end of the TX descriptor, we can jump to the next buffer */
        if (min == out_list.config.length) {

            /* Before jumping to the next node, clear the owner bit if needed */
            if (clear_out) {
                out_list.config.owner = 0;

                /* Write back the modified descriptor, should always be valid */
                valid = esp32c3_gdma_read_descr(s, out_addr, &out_list);
                assert(valid);
            }

            const bool eof_bit = out_list.config.suc_eof;

            /* Retrieve the next node  while updating the virtual guest address */
            out_addr = out_list.next_addr;
            valid = esp32c3_gdma_next_list_node(s, chan, ESP32C3_GDMA_OUT_IDX, &out_list);

            /* Only check the valid flag and the owner if we don't have to exit the loop*/
            if ( !exit_loop && (!valid || (owner_check_out && !out_list.config.owner)) ) {
                esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
                error = true;
            }

            /* If the EOF bit was set, the real controller doesn't stop the transfer, it simply
             * sets the status accordingly (and generates an interrupt if enabled) */
            if (eof_bit) {
                esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_EOF_CH0_INT_RAW_MASK |
                                                 R_DMA_INT_RAW_CH0_OUT_TOTAL_EOF_CH0_INT_RAW_MASK);
            }
        }
    }

    /* Check if all the bytes were sent successfully */
    if (exit_loop && consumed != size) {
        /* TODO: which error should be triggered ?*/
        esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
        error = true;
    }

    if (!error) {
        /* Set the transfer as completed. EOF should have already been triggered within the loop */
        esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DONE_CH0_INT_RAW_MASK);
    }

    return !error;
}


/**
 * @brief Write data to the guest RAM pointed by the linked list configured in the given DmaConfigState index.
 *        `size` bytes from `buffer` will be written to guest machine's RAM.
 */
bool esp32c3_gdma_write_channel(ESP32C3GdmaState *s, uint32_t chan, uint8_t* buffer, uint32_t size)
{
    DmaConfigState* state = &s->ch_conf[chan][ESP32C3_GDMA_IN_IDX];

    /* Clear the (RE)START fields, i.e., only keep the link address */
    state->link &= R_DMA_OUT_LINK_CH0_OUTLINK_ADDR_CH0_MASK;

    /* Same goes for the status */
    esp32c3_gdma_clear_status(s, chan, R_DMA_INT_RAW_CH0_IN_DONE_CH0_INT_RAW_MASK  |
                                       R_DMA_INT_RAW_CH0_IN_SUC_EOF_CH0_INT_RAW_MASK);

    /* Get highest 12 bits of the DRAM address */
    uint32_t in_addr = ((ESP32C3_GDMA_RAM_ADDR >> 20) << 20) | FIELD_EX32(state->link, DMA_IN_LINK_CH0, INLINK_ADDR_CH0);

    /* Boolean to mark whether we need to check the owner for in buffers */
    const bool owner_check_in = FIELD_EX32(state->conf1, DMA_IN_CONF1_CH0, IN_CHECK_OWNER_CH0);

    /* Pointer to the lists that will be browsed by the loop below */
    GdmaLinkedList in_list = { 0 };
    /* Boolean to mark whether a descriptor error occurred during the transfer */
    bool valid = true;

    valid = esp32c3_gdma_read_descr(s, in_addr, &in_list);
    esp32c3_gdma_push_descriptor(s, chan, ESP32C3_GDMA_IN_IDX, in_addr);

    if ( !valid || (owner_check_in && !in_list.config.owner) ) {
        esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_ERR_CH0_INT_RAW_MASK);
        return false;
    }

    /* Clear the number of bytes written to the "in" buffer and the owner */
    in_list.config.length = 0;

    uint32_t consumed = 0;
    bool exit_loop = false;
    bool error = false;

    while (!exit_loop && !error) {

        /* Calculate the number of bytes to write to the in channel */
        const uint32_t remaining = size - consumed;
        const uint32_t min = MIN(in_list.config.size, remaining);

        /* Perform the actual copy, the in buffer address will always be at the beginning because the data
         * to write to it are contiguous (`buffer` parameter) */
        valid = esp32c3_gdma_write_guest(s, in_list.buf_addr, buffer + consumed, min);
        if (!valid) {
            esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_ERR_CH0_INT_RAW_MASK);
            error = true;
        }

        /* Update the number of bytes written to the "in" buffer */
        in_list.config.length += min;
        consumed += min;

        if (size == consumed) {
            exit_loop = true;
        }

        /* If we reached the end of the "node", go to the next one */
        if (in_list.config.size == in_list.config.length) {
            /* Clear the owner bit, set the length to the maximum bytes readable */
            in_list.config.owner = 0;

            /* During peripheral-to-memory transfers, the eof bit is only used to set a status bit, and generate
             * an interrupt if enabled. If we still have bytes to send, we won't stop the transfer.
             * In all cases, reset this bit as it must be only set at the end of the buffer. */
            if (in_list.config.suc_eof) {
                in_list.config.suc_eof = 0;
                esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_SUC_EOF_CH0_INT_RAW_MASK);
            }

            /* Write back the IN node to guest RAM */
            valid = esp32c3_gdma_write_descr(s, in_addr, &in_list);
            assert(valid);

            /* Get the next virtual address before replacing the current list node content */
            const uint32_t next_addr = in_list.next_addr;

            /* Even if we have to exit the loop, we still have to push the next address to the descriptors stack */
            if (exit_loop) {
                esp32c3_gdma_push_descriptor(s, chan, ESP32C3_GDMA_IN_IDX, next_addr);
                break;
            }

            /* In the case where the transfer is finished, we should still fetch the next node,
             * but we should not override the current in_list variable as it is used outside the loop
             * to reset the owner and update the suc_eof flag */
            valid = esp32c3_gdma_next_list_node(s, chan, ESP32C3_GDMA_IN_IDX, &in_list);

            if (!valid || (owner_check_in && !in_list.config.owner)) {
                /* Check the validity of the next node if we have to continue the loop (transfer finished) */
                esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_ERR_CH0_INT_RAW_MASK);
                error = true;
            } else {
                /* Continue the loop normally, next RX descriptor set to current */
                in_list.config.length = 0;

                /* Update the current in guest address */
                in_addr = next_addr;
            }
        }
    }

    if (!error) {
        /* In all cases (error or not), let's set the End-of-list in the receiver */
        in_list.config.suc_eof = 1;
        in_list.config.owner = 0;

        valid = esp32c3_gdma_write_descr(s, in_addr, &in_list);
        assert(valid);

        /* And store the EOF RX descriptor GUEST address in the correct register.
         * This can be used in the ISR to know which buffer has just been processed. */
        state->suc_eof_desc_addr = in_addr;

        /* Set the transfer as completed for both the IN and OUT link */
        esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DONE_CH0_INT_RAW_MASK);
    }

    return !error;
}


/**
 * @brief Check if a memory-to-memory transfer can be started and start it if possible
 *
 * @param s GDMA state structure
 * @param chan Index of the channel
 */
static void esp32c3_gdma_check_and_start_mem_transfer(ESP32C3GdmaState *s, uint32_t chan)
{
    DmaConfigState* state = s->ch_conf[chan];
    /* Keep the distinction between start and restart because it influences the first descriptor to process */
    const bool in_start    = FIELD_EX32(state[ESP32C3_GDMA_IN_IDX].link, DMA_IN_LINK_CH0, INLINK_START_CH0)   ? true : false;
    const bool in_restart  = FIELD_EX32(state[ESP32C3_GDMA_IN_IDX].link, DMA_IN_LINK_CH0, INLINK_RESTART_CH0) ? true : false;
    const bool out_start   = FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].link, DMA_OUT_LINK_CH0, OUTLINK_START_CH0)   ? true : false;
    const bool out_restart = FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].link, DMA_OUT_LINK_CH0, OUTLINK_RESTART_CH0) ? true : false;

    /* A memory-to-memory transfer can be started if MEM_TRANS is enabled, OUTLINK_(RE)START is set
     * and INLINK_(RE)START is set */
    if (FIELD_EX32(state[ESP32C3_GDMA_IN_IDX].conf0, DMA_IN_CONF0_CH0, MEM_TRANS_EN_CH0)
        && (in_start  || in_restart)
        && (out_start || out_restart))
    {
        /* Clear the (RE)START fields, i.e., only keep the link address */
        state[ESP32C3_GDMA_OUT_IDX].link &= R_DMA_OUT_LINK_CH0_OUTLINK_ADDR_CH0_MASK;
        state[ESP32C3_GDMA_IN_IDX].link &= R_DMA_IN_LINK_CH0_INLINK_ADDR_CH0_MASK;
        /* Same goes for the status */
        esp32c3_gdma_clear_status(s, chan, R_DMA_INT_RAW_CH0_IN_DONE_CH0_INT_RAW_MASK  |
                                           R_DMA_INT_RAW_CH0_OUT_DONE_CH0_INT_RAW_MASK |
                                           R_DMA_INT_RAW_CH0_OUT_EOF_CH0_INT_RAW_MASK  |
                                           R_DMA_INT_RAW_CH0_IN_SUC_EOF_CH0_INT_RAW_MASK);

        /* Get highest 12 bits of the DRAM address */
        const uint32_t high = (ESP32C3_GDMA_RAM_ADDR >> 20) << 20;

        /* TODO: in an inlink, when burst mode is enabled, size and buffer address must be word-aligned. */
        /* If a start was performed, the first descriptor address to process is in DMA_OUT_LINK_CHn register,
         * if a restart was performed, the first buffer is the `next` node of `desc_addr` register */
        uint32_t out_addr = high;
        uint32_t in_addr = high;

        if (out_start) {
            out_addr |= FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].link, DMA_OUT_LINK_CH0, OUTLINK_ADDR_CH0);
        } else {
            esp32c3_gdma_get_restart_buffer(s, chan, ESP32C3_GDMA_OUT_IDX, &out_addr);
        }

        if (in_start) {
            in_addr |= FIELD_EX32(state[ESP32C3_GDMA_IN_IDX].link, DMA_IN_LINK_CH0, INLINK_ADDR_CH0);
        } else {
            esp32c3_gdma_get_restart_buffer(s, chan, ESP32C3_GDMA_IN_IDX, &in_addr);
        }

        /* Boolean to mark whether we need to check the owner for in and out buffers */
        const bool owner_check_out = FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].conf1, DMA_OUT_CONF1_CH0, OUT_CHECK_OWNER_CH0);
        const bool owner_check_in = FIELD_EX32(state[ESP32C3_GDMA_IN_IDX].conf1, DMA_IN_CONF1_CH0, IN_CHECK_OWNER_CH0);
        /* Boolean to mark whether the transmit (out) buffers must have their owner bit cleared here */
        const bool clear_out = FIELD_EX32(state[ESP32C3_GDMA_OUT_IDX].conf0, DMA_OUT_CONF0_CH0, OUT_AUTO_WRBACK_CH0);

        /* Pointer to the lists that will be browsed by the loop below */
        GdmaLinkedList out_list = { 0 };
        GdmaLinkedList in_list = { 0 };
        /* Boolean to mark whether a descriptor error occurred during the transfer */
        bool valid = true;
        bool error = false;

        /* Get the content of the descriptor located at guest address out_addr */
        valid = esp32c3_gdma_read_descr(s, out_addr, &out_list);
        esp32c3_gdma_push_descriptor(s, chan, ESP32C3_GDMA_OUT_IDX, out_addr);

        /* Check that the address is valid. If the owner must be checked, make sure owner is the DMA controller.
         * On the real hardware, both in and out are checked at the same time, so in case of an error, both bits
         * are set. Replicate the same behavior here. */
        if ( !valid || (owner_check_out && !out_list.config.owner) ) {
            /* In case of an error, go directly to the next node (as the C3 hardware does) */
            esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
            error = true;
        }

        valid = esp32c3_gdma_read_descr(s, in_addr, &in_list);
        esp32c3_gdma_push_descriptor(s, chan, ESP32C3_GDMA_IN_IDX, in_addr);

        if ( !valid || (owner_check_in && !in_list.config.owner) ) {
            esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_ERR_CH0_INT_RAW_MASK);
            error = true;
        }

        /* If any of the error bit has been set, return directly */
        if (error) {
            return;
        }

        /* We can keep track of the total amount of bytes written in order to simulate a more or less
         * accurate timing (and an interrupt) */
        int total = 0;

        /* Clear the number of bytes written to the "in" buffer and the owner */
        in_list.config.length = 0;

        /* Number of bytes remaining in the current "out" buffer */
        uint32_t remaining = out_list.config.length;
        /* Store the current number of bytes consumed in the "out" buffer */
        uint32_t consumed = 0;

        bool exit_loop = false;

        /* Allocate a temporary buffer big enough to store any descriptor data */
        void* tmp_buffer = g_malloc(4096 * sizeof(uint8_t));
        if (tmp_buffer == NULL) {
            error_report("[GDMA] No more memory in host\n");
            return;
        }

        while (!exit_loop && !error) {

            /* Calculate the number of bytes to send to the in channel */
            const uint32_t min = MIN(in_list.config.size, out_list.config.length);

            /* Perform the actual copy, for the same reasons as stated above, use the error boolean */
            valid = esp32c3_gdma_read_guest(s, out_list.buf_addr + consumed, tmp_buffer, min);
            if (!valid) {
                esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
                error = true;
            }
            valid = esp32c3_gdma_write_guest(s, in_list.buf_addr + in_list.config.length, tmp_buffer, min);
            if (!valid) {
                esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_ERR_CH0_INT_RAW_MASK);
                error = true;
            }

            /* Update the number of bytes written to the "in" buffer */
            in_list.config.length += min;
            consumed += min;
            total += min;

            /* Even if we reached the end of the TX descriptor, we still have to update RX descriptors
             * and registers, use `exit_loop` instead of break or return */
            /* If we don't have any more bytes in the "out" buffer, we can skip to the next buffer */
            if (remaining == consumed) {
                /* Before jumping to the next node, clear the owner bit */
                if (clear_out) {
                    out_list.config.owner = 0;
                    /* Write back the modified descriptor, should always be valid */
                    valid = esp32c3_gdma_write_descr(s, out_addr, &out_list);
                    assert(valid);
                }
                exit_loop = out_list.config.suc_eof ? true : false;

                const uint32_t next_addr = out_list.next_addr;
                valid = esp32c3_gdma_next_list_node(s, chan, ESP32C3_GDMA_OUT_IDX, &out_list);

                /* Only check the valid flag and the owner if we don't have to exit the loop*/
                if ( !exit_loop && (!valid || (owner_check_out && !out_list.config.owner)) ) {
                    esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_OUT_DSCR_ERR_CH0_INT_RAW_MASK);
                    error = true;
                } else {
                    /* Update "remaining" with the number of bytes to transfer from the new buffer */
                    out_addr = next_addr;
                    remaining = out_list.config.length;
                    consumed = 0;
                }
            }


            /* If we reached the end of the "node", go to the next one */
            if (in_list.config.size == in_list.config.length) {

                in_list.config.owner = 0;

                /* Write back the IN node to guest RAM */
                valid = esp32c3_gdma_write_descr(s, in_addr, &in_list);
                assert(valid);

                /* Check that we do have more "in" buffers, if that's not the case, raise an error..
                 * TODO: Check if the behavior is the same as Peripheral-to-Memory transfers, where
                 * this bit is only used to generate and interrupt. */
                if (!exit_loop && in_list.config.suc_eof) {
                    esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_EMPTY_CH0_INT_RAW_MASK);
                    error = true;
                    break;
                }

                const uint32_t next_addr = in_list.next_addr;

                /* In the case where the transfer is finished, we should still "push" the next node
                 * to our descriptors stack, but we should not modify the structure itself as we will
                 * reset the owner and update the suc_eof flag */
                if (exit_loop) {
                    esp32c3_gdma_push_descriptor(s, chan, ESP32C3_GDMA_IN_IDX, next_addr);
                    break;
                }

                /* We have to continue the loop, so fetch the next node, it will also update the descriptors stack */
                valid = esp32c3_gdma_next_list_node(s, chan, ESP32C3_GDMA_IN_IDX, &in_list);

                /* Check the validity of the next node if we have to continue the loop (transfer finished) */
                if (!valid || (owner_check_in && !in_list.config.owner)) {
                    esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DSCR_ERR_CH0_INT_RAW_MASK);
                    error = true;
                } else {
                    /* Continue the loop normally, next RX descriptor set to current */
                    in_list.config.length = 0;

                    /* Update the current in guest address */
                    in_addr = next_addr;
                }
            }

        }

        if (!error) {
            /* In all cases (error or not), let's set the End-of-list in the receiver */
            in_list.config.suc_eof = 1;
            in_list.config.owner = 0;

            /* Write back the previous changes */
            valid = esp32c3_gdma_write_descr(s, in_addr, &in_list);
            assert(valid);

            /* And store the EOF RX descriptor GUEST address in the correct register.
             * This can be used in the ISR to know which buffer has just been processed. */
            state[ESP32C3_GDMA_IN_IDX].suc_eof_desc_addr = in_addr;

            /* Set the transfer as completed for both the IN and OUT link */
            esp32c3_gdma_set_status(s, chan, R_DMA_INT_RAW_CH0_IN_DONE_CH0_INT_RAW_MASK  |
                                             R_DMA_INT_RAW_CH0_OUT_DONE_CH0_INT_RAW_MASK |
                                             R_DMA_INT_RAW_CH0_OUT_EOF_CH0_INT_RAW_MASK  |
                                             R_DMA_INT_RAW_CH0_IN_SUC_EOF_CH0_INT_RAW_MASK);
        }

        g_free(tmp_buffer);
    }
}



/**
 * @brief Check whether the given register offset corresponds to a read-only register
 *
 * @param offset Offset of the register in DmaConfigState structure, in bytes
 *
 * @return true is the register is read-only, false else
 */
static bool esp32c3_gdma_register_read_only(uint32_t offset)
{
    return offset >= offsetof(DmaConfigState, state) && offset < offsetof(DmaConfigState, priority);
}


/**
 * @brief Function called when a configuration register was written to
 *
 * @param s GDMA state structure
 * @param chan Index of the channel to be written
 * @param in_out Index of the sub-channel, which is ESP32C3_GDMA_IN_IDX or ESP32C3_GDMA_OUT_IDX
 * @param addr_in_block Address in bytes of the register being written to in the block
 * @param value 32-bit value being written to the register
 */
static void esp32c3_gdma_write_chan_conf(ESP32C3GdmaState *s, uint32_t chan, uint32_t in_out,
                                         uint32_t addr_in_block, uint32_t value)
{
    DmaConfigState* state = &s->ch_conf[chan][in_out];
    uint32_t* const reg_addr = (uint32_t*) ((uint8_t*) state + addr_in_block);

    /* Dereference the former value of the register being written to */
    const uint32_t former = *reg_addr;

    /* Write the new value to the register if not read-only! */
    if (!esp32c3_gdma_register_read_only(addr_in_block)) {
        *reg_addr = value;
    }

    /* We will only support a subset of GDMA registers for now. To add support for more registers,
     * the following snippet can be update */
    uint32_t start_mask = 0;
    uint32_t restart_mask = 0;
    switch(addr_in_block) {

        /* No matter the channel and in/out direction, the registers are organized the same way,
         * so we can use the macros for any channel */
        case offsetof(DmaConfigState, conf0):
            /* Check the reset bit, call the reset function on negative edge */
            if (FIELD_EX32(value,  DMA_IN_CONF0_CH0, IN_RST_CH0) == 0 &&
                FIELD_EX32(former, DMA_IN_CONF0_CH0, IN_RST_CH0) != 0)
            {
                esp32c3_gdma_reset_fifo(s, chan, in_out);
            }
            /* Check if memory transfer has just been enabled (only valid for IN channels) */
            if (in_out == ESP32C3_GDMA_IN_IDX &&
                FIELD_EX32(value,  DMA_IN_CONF0_CH0, MEM_TRANS_EN_CH0))
            {
                esp32c3_gdma_check_and_start_mem_transfer(s, chan);
            }
            break;

        case offsetof(DmaConfigState, link):
            /* For IN and OUT, the START bit is not at the same offset, so we need to test both separately */
            start_mask = ESP32C3_GDMA_IN_IDX ? R_DMA_IN_LINK_CH0_INLINK_START_CH0_MASK : R_DMA_OUT_LINK_CH0_OUTLINK_START_CH0_MASK;
            restart_mask = ESP32C3_GDMA_IN_IDX ? R_DMA_IN_LINK_CH0_INLINK_RESTART_CH0_MASK : R_DMA_OUT_LINK_CH0_OUTLINK_RESTART_CH0_MASK;

            /* Check if any of the previous two bits has just been enabled */
            if ((value & start_mask) || (value & restart_mask))
            {
                esp32c3_gdma_check_and_start_mem_transfer(s, chan);
            }

        default:
            break;
    }
}


static void esp32c3_gdma_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned int size)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(opaque);

#if GDMA_DEBUG
    info_report("[GDMA] Writing to %08lx (%08lx)", addr, value);
#endif

    switch(addr) {
        case A_DMA_INT_RAW_CH0 ... A_DMA_INT_CLR_CH2:
            esp32c3_gdma_write_int_state(s,
                                         addr / ESP32C3_GDMA_INT_REGS_SIZE,
                                         addr % ESP32C3_GDMA_INT_REGS_SIZE,
                                         value);
            break;

        case A_DMA_MISC_CONF:
            s->misc_conf = value;
            break;

        case A_DMA_IN_CONF0_CH0 ... A_DMA_IN_PERI_SEL_CH0:
            esp32c3_gdma_write_chan_conf(s, 0, ESP32C3_GDMA_IN_IDX, addr - A_DMA_IN_CONF0_CH0, value);
            break;

        case A_DMA_OUT_CONF0_CH0 ... A_DMA_OUT_PERI_SEL_CH0:
            esp32c3_gdma_write_chan_conf(s, 0, ESP32C3_GDMA_OUT_IDX, addr - A_DMA_OUT_CONF0_CH0, value);
            break;

        case A_DMA_IN_CONF0_CH1 ... A_DMA_IN_PERI_SEL_CH1:
            esp32c3_gdma_write_chan_conf(s, 1, ESP32C3_GDMA_IN_IDX, addr - A_DMA_IN_CONF0_CH1, value);
            break;

        case A_DMA_OUT_CONF0_CH1 ... A_DMA_OUT_PERI_SEL_CH1:
            esp32c3_gdma_write_chan_conf(s, 1, ESP32C3_GDMA_OUT_IDX, addr - A_DMA_OUT_CONF0_CH1, value);
            break;

        case A_DMA_IN_CONF0_CH2 ... A_DMA_IN_PERI_SEL_CH2:
            esp32c3_gdma_write_chan_conf(s, 2, ESP32C3_GDMA_IN_IDX, addr - A_DMA_IN_CONF0_CH2, value);
            break;

        case A_DMA_OUT_CONF0_CH2 ... A_DMA_OUT_PERI_SEL_CH2:
            esp32c3_gdma_write_chan_conf(s, 2, ESP32C3_GDMA_OUT_IDX, addr - A_DMA_OUT_CONF0_CH2, value);
            break;

        default:
#if GDMA_WARNING
            warn_report("[GDMA] Unsupported write to %08lx (%08lx)", addr, value);
#endif
            break;
    }

}

static const MemoryRegionOps esp32c3_gdma_ops = {
    .read =  esp32c3_gdma_read,
    .write = esp32c3_gdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static Property esp32c3_gdma_properties[] = {
    DEFINE_PROP_LINK("soc_mr", ESP32C3GdmaState, soc_mr, TYPE_MEMORY_REGION, MemoryRegion*),
    DEFINE_PROP_END_OF_LIST(),
};


static void esp32c3_gdma_reset(DeviceState *dev)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(dev);
    memset(s->ch_int, 0, sizeof(s->ch_int));
    memset(s->ch_conf, 0, sizeof(s->ch_conf));
    s->misc_conf = 0;

    /* Set the FIFOs as empty */
    for (int i = 0; i < ESP32C3_GDMA_CHANNEL_COUNT; i++) {
        qemu_irq_lower(s->irq[i]);

        for (int j = 0; j < ESP32C3_GDMA_CONF_COUNT; j++) {
            esp32c3_gdma_reset_fifo(s, i, j);
        }
    }

}


static void esp32c3_gdma_realize(DeviceState *dev, Error **errp)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(dev);

    /* Make sure the DRAM MemoryRegion was set */
    assert(s->soc_mr != NULL);

    address_space_init(&s->dma_as, s->soc_mr, "esp32c3.gdma");
}


static void esp32c3_gdma_init(Object *obj)
{
    ESP32C3GdmaState *s = ESP32C3_GDMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_gdma_ops, s,
                          TYPE_ESP32C3_GDMA, ESP32C3_GDMA_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    for (int i = 0; i < ESP32C3_GDMA_CHANNEL_COUNT; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    esp32c3_gdma_reset((DeviceState*) s);
}


static void esp32c3_gdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32c3_gdma_reset;
    dc->realize = esp32c3_gdma_realize;
    device_class_set_props(dc, esp32c3_gdma_properties);
}

static const TypeInfo esp32c3_gdma_info = {
        .name = TYPE_ESP32C3_GDMA,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(ESP32C3GdmaState),
        .instance_init = esp32c3_gdma_init,
        .class_init = esp32c3_gdma_class_init
};

static void esp32c3_gdma_register_types(void)
{
    type_register_static(&esp32c3_gdma_info);
}

type_init(esp32c3_gdma_register_types)
