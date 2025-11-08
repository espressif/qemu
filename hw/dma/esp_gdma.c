/*
 * ESP GDMA emulation
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
#include "hw/dma/esp_gdma.h"
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


/**
 * @brief Check whether the new status of any interrupt should trigger an interrupt
 *
 * @param s GDMA state structure
 * @param chan Channel that has just been updated
 */
static void esp_gdma_check_interrupt_status(DmaIntState* int_st)
{
    const uint32_t former = int_st->st;

    /* Calculate the new status and check for any difference */
    int_st->st = int_st->raw & int_st->ena;

    if (former != int_st->st) {
        /* If all the status bits became low, lower the IRQ pin, else, raise it  */
        qemu_set_irq(int_st->irq, int_st->st ? 1 : 0);
    }
}


/**
 * @brief Set the status bit for the given channel. If the status triggers an interrupt, the corresponding
 * IRQ will be set.
*/
static void esp_gdma_set_status(DmaIntState* state, uint32_t mask)
{
    state->raw |= mask;
    esp_gdma_check_interrupt_status(state);
}



/**
 * @brief Clear the status bit for the given channel
*/
static void esp_gdma_clear_status(DmaIntState* state, uint32_t mask)
{
    state->raw &= ~mask;
    esp_gdma_check_interrupt_status(state);
}


/**
 * @brief Function called when a write to a channel interrupt register is performed
 *
 * @param s GDMA state structure
 * @param chan Index of the channel to be written
 * @param reg Offset, in bytes, of the register to modify
 * @param value New value for the register
 */
static void esp_gdma_write_int_state(DmaIntState* state, DmaRegister reg, uint32_t value)
{
    switch (reg) {
        case GDMA_INT_ENA_REG:
            state->ena = value;
            break;

        case GDMA_INT_RAW_REG:
        case GDMA_INT_CLR_REG:
            /* Clear the bits that are set to 1, keep the remaining to their original value */
            state->raw &= ~value;
            break;

        default:
            /* Nothing to do, read-only register, return directly */
            return;
    }

    /* Update the status and check if any interrupt needs to occur */
    esp_gdma_check_interrupt_status(state);
}


/**
 * @brief Function called when a reset FIFO is requested
 *
 * @param s GDMA state structure
 * @param chan Index of the channel
 * @param in_out Index of the direction, ESP_GDMA_IN_IDX or ESP_GDMA_OUT_IDX,
 *        that needs a FIFO reset
 */
static void esp_gdma_reset_fifo(DmaConfigState* s)
{
#if GDMA_DEBUG
    info_report("Resetting FIFO for chan %d, direction: %d", chan, in_out);
#endif
    /* Set the FIFO empty bit to 1, full bit to 0, and number of bytes of data to 0 */
    s->status = R_GDMA_INFIFO_STATUS_FIFO_EMPTY_MASK;
}


/**
 * @brief Read a descriptor from the guest machine
 *
 * @param s GDMA state structure
 * @param addr Guest machine address
 *
 * @returns true if the transfer was a success, false else
 */
static bool esp_gdma_read_descr(ESPGdmaState *s, uint32_t addr, GdmaLinkedList* out)
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
static bool esp_gdma_write_descr(ESPGdmaState *s, uint32_t addr, GdmaLinkedList* in)
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
static bool esp_gdma_read_guest(ESPGdmaState *s, uint32_t addr, void* data, uint32_t len)
{
    MemTxResult res = dma_memory_read(&s->dma_as, addr, data, len, MEMTXATTRS_UNSPECIFIED);
    return res == MEMTX_OK;
}

static bool esp_gdma_write_guest(ESPGdmaState *s, uint32_t addr, void* data, uint32_t len)
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
static void esp_gdma_push_descriptor(ESPGdmaState *s, uint32_t chan, uint32_t dir, uint32_t current)
{
    GdmaLinkedList next_node;
    uint32_t next = 0;
    DmaConfigState* state = &s->ch_conf[dir][chan];

    /* Assign the current descriptor address to the state register */
    state->state = current & R_GDMA_OUT_STATE_LINK_DSCR_ADDR_MASK;

    /* On real hardware, if the former address is incorrect, the current address is copied to this
     * register. */
    state->bfr_bfr_desc_addr = state->bfr_desc_addr;
    /* On real hardware, state->bfr_desc_addr is taken from state->desc_addr, even is `current` is valid */
    state->bfr_desc_addr = state->desc_addr;

    /* Get the next address out of the guest RAM */
    const bool valid = esp_gdma_read_descr(s, current, &next_node);
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
static inline bool esp_gdma_next_list_node(ESPGdmaState *s, uint32_t chan, uint32_t dir, GdmaLinkedList* node)
{
    const uint32_t current = node->next_addr;
    esp_gdma_push_descriptor(s, chan, dir, current);
    return esp_gdma_read_descr(s, current, node);
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
static void esp_gdma_get_restart_buffer(ESPGdmaState *s, uint32_t chan, uint32_t dir, uint32_t* out)
{
    DmaConfigState* state = &s->ch_conf[dir][chan];
    // GdmaLinkedList* list = NULL;
    /* The next node to use is taken from state->state's lowest 18 bit. Append it to the DRAM address */
    const uint32_t dram_upper_bits = ESP_GDMA_RAM_ADDR & (~R_GDMA_OUT_STATE_LINK_DSCR_ADDR_MASK);
    const uint32_t guest_addr = dram_upper_bits | FIELD_EX32(state->state, GDMA_OUT_STATE, LINK_DSCR_ADDR);

    *out = guest_addr;
}


/**
 * Check the header file for more info about this function
 */
bool esp_gdma_get_channel_periph(ESPGdmaState *s, GdmaPeripheral periph, int dir, uint32_t* chan)
{
    const ESPGdmaClass* class = ESP_GDMA_GET_CLASS(s);

    /* If the state, the peripheral or the direction is invalid, return directly */
    if (s == NULL || chan == NULL || periph > GDMA_LAST || dir < 0 || dir >= ESP_GDMA_CONF_COUNT ||
        (class->is_periph_invalid && class->is_periph_invalid(s, periph)))
    {
        return false;
    }

    /* Check all the channels of the GDMA */
    for (int i = 0; i < class->m_channel_count; i++) {
        /* IN/OUT PERI registers have the same organization, can use any macro.
         * Look for the channel that was configured with the given peripheral. It must be marked as "started" too */
        if ( FIELD_EX32(s->ch_conf[dir][i].peripheral, GDMA_PERI_SEL, PERI_SEL) == periph ||
             FIELD_EX32(s->ch_conf[dir][i].link, GDMA_OUT_LINK, START)) {

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
bool esp_gdma_read_channel(ESPGdmaState *s, uint32_t chan, uint8_t* buffer, uint32_t size)
{
    DmaConfigState* state = &s->ch_conf[ESP_GDMA_OUT_IDX][chan];

    state->link &= R_GDMA_OUT_LINK_ADDR_MASK;

    /* Same goes for the status */
    esp_gdma_clear_status(&state->int_state, R_GDMA_INTERRUPT_OUT_DONE_MASK |
                                             R_GDMA_INTERRUPT_OUT_EOF_MASK);

    /* Get the guest DRAM address */
    uint32_t out_addr = ((ESP_GDMA_RAM_ADDR >> 20) << 20) | FIELD_EX32(state->link, GDMA_OUT_LINK, ADDR);

    /* Boolean to mark whether we need to check the owner for in and out buffers */
    const bool owner_check_out = FIELD_EX32(state->conf1, GDMA_OUT_CONF1, CHECK_OWNER);

    /* Boolean to mark whether the transmit (out) buffers must have their owner bit cleared here */
    const bool clear_out = FIELD_EX32(state->conf0, GDMA_OUT_CONF0, AUTO_WRBACK);

    /* Pointer to the lists that will be browsed by the loop below */
    GdmaLinkedList out_list;

    /* Boolean to mark whether a descriptor error occurred during the transfer */
    bool valid = true;

    /* Set the current buffer (guest address) in the `desc_addr` register */
    valid = esp_gdma_read_descr(s, out_addr, &out_list);
    esp_gdma_push_descriptor(s, chan, ESP_GDMA_OUT_IDX, out_addr);

    /* Check that the address is valid. If the owner must be checked, make sure owner is the DMA controller.
     * On the real hardware, both in and out are checked at the same time, so in case of an error, both bits
     * are set. Replicate the same behavior here. */
    if ( !valid || (owner_check_out && !out_list.config.owner) ) {
        esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
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

        valid = esp_gdma_read_guest(s, out_list.buf_addr, buffer + consumed, min);
        if (!valid) {
            esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
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
                valid = esp_gdma_write_descr(s, out_addr, &out_list);
                assert(valid);
            }

            const bool eof_bit = out_list.config.suc_eof;

            /* Retrieve the next node  while updating the virtual guest address */
            out_addr = out_list.next_addr;
            valid = esp_gdma_next_list_node(s, chan, ESP_GDMA_OUT_IDX, &out_list);

            /* Only check the valid flag and the owner if we don't have to exit the loop*/
            if ( !exit_loop && (!valid || (owner_check_out && !out_list.config.owner)) ) {
                esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
                error = true;
            }

            /* If the EOF bit was set, the real controller doesn't stop the transfer, it simply
             * sets the status accordingly (and generates an interrupt if enabled) */
            if (eof_bit) {
                esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_OUT_EOF_MASK |
                                                       R_GDMA_INTERRUPT_OUT_TOTAL_EOF_MASK);
            }
        }
    }

    /* Check if all the bytes were sent successfully */
    if (exit_loop && consumed != size) {
        /* TODO: which error should be triggered ?*/
        esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
        error = true;
    }

    if (!error) {
        /* Set the transfer as completed. EOF should have already been triggered within the loop */
        esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_OUT_DONE_MASK);
    }

    return !error;
}


/**
 * @brief Write data to the guest RAM pointed by the linked list configured in the given DmaConfigState index.
 *        `size` bytes from `buffer` will be written to guest machine's RAM.
 */
bool esp_gdma_write_channel(ESPGdmaState *s, uint32_t chan, uint8_t* buffer, uint32_t size)
{
    DmaConfigState* state = &s->ch_conf[ESP_GDMA_IN_IDX][chan];

    /* Clear the (RE)START fields, i.e., only keep the link address */
    state->link &= R_GDMA_OUT_LINK_ADDR_MASK;

    /* Same goes for the status */
    esp_gdma_clear_status(&state->int_state, R_GDMA_INTERRUPT_IN_DONE_MASK  |
                                             R_GDMA_INTERRUPT_IN_SUC_EOF_MASK);

    /* Get highest 12 bits of the DRAM address */
    uint32_t in_addr = ((ESP_GDMA_RAM_ADDR >> 20) << 20) | FIELD_EX32(state->link, GDMA_IN_LINK, ADDR);

    /* Boolean to mark whether we need to check the owner for in buffers */
    const bool owner_check_in = FIELD_EX32(state->conf1, GDMA_IN_CONF1, CHECK_OWNER);

    /* Pointer to the lists that will be browsed by the loop below */
    GdmaLinkedList in_list = { 0 };
    /* Boolean to mark whether a descriptor error occurred during the transfer */
    bool valid = true;

    valid = esp_gdma_read_descr(s, in_addr, &in_list);
    esp_gdma_push_descriptor(s, chan, ESP_GDMA_IN_IDX, in_addr);

    if ( !valid || (owner_check_in && !in_list.config.owner) ) {
        esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK);
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
        valid = esp_gdma_write_guest(s, in_list.buf_addr, buffer + consumed, min);
        if (!valid) {
            esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK);
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
                esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_IN_SUC_EOF_MASK);
            }

            /* Write back the IN node to guest RAM */
            valid = esp_gdma_write_descr(s, in_addr, &in_list);
            assert(valid);

            /* Get the next virtual address before replacing the current list node content */
            const uint32_t next_addr = in_list.next_addr;

            /* Even if we have to exit the loop, we still have to push the next address to the descriptors stack */
            if (exit_loop) {
                esp_gdma_push_descriptor(s, chan, ESP_GDMA_IN_IDX, next_addr);
                break;
            }

            /* In the case where the transfer is finished, we should still fetch the next node,
             * but we should not override the current in_list variable as it is used outside the loop
             * to reset the owner and update the suc_eof flag */
            valid = esp_gdma_next_list_node(s, chan, ESP_GDMA_IN_IDX, &in_list);

            if (!valid || (owner_check_in && !in_list.config.owner)) {
                /* Check the validity of the next node if we have to continue the loop (transfer finished) */
                esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK);
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

        valid = esp_gdma_write_descr(s, in_addr, &in_list);
        assert(valid);

        /* And store the EOF RX descriptor GUEST address in the correct register.
         * This can be used in the ISR to know which buffer has just been processed. */
        state->suc_eof_desc_addr = in_addr;

        /* Set the transfer as completed for both the IN and OUT link */
        esp_gdma_set_status(&state->int_state, R_GDMA_INTERRUPT_IN_DONE_MASK);
    }

    return !error;
}


/**
 * @brief Check if a memory-to-memory transfer can be started and start it if possible
 *
 * @param s GDMA state structure
 * @param chan Index of the channel
 */
static void esp_gdma_check_and_start_mem_transfer(ESPGdmaState *s, uint32_t chan)
{
    DmaConfigState* state_in  = &s->ch_conf[ESP_GDMA_IN_IDX][chan];
    DmaConfigState* state_out = &s->ch_conf[ESP_GDMA_OUT_IDX][chan];
    /* Keep the distinction between start and restart because it influences the first descriptor to process */
    const bool in_start    = FIELD_EX32(state_in->link,  GDMA_IN_LINK,  START)    ? true : false;
    const bool in_restart  = FIELD_EX32(state_in->link,  GDMA_IN_LINK,  RESTART)  ? true : false;
    const bool out_start   = FIELD_EX32(state_out->link, GDMA_OUT_LINK, START)    ? true : false;
    const bool out_restart = FIELD_EX32(state_out->link, GDMA_OUT_LINK, RESTART)  ? true : false;

    /* A memory-to-memory transfer can be started if MEM_TRANS is enabled, OUTLINK_(RE)START is set
     * and INLINK_(RE)START is set */
    if (FIELD_EX32(state_in->conf0, GDMA_IN_CONF0, MEM_TRANS_EN)
        && (in_start  || in_restart)
        && (out_start || out_restart))
    {
        /* Clear the (RE)START fields, i.e., only keep the link address */
        state_out->link &= R_GDMA_OUT_LINK_ADDR_MASK;
        state_in->link  &= R_GDMA_IN_LINK_ADDR_MASK;
        /* Same goes for the status */
        esp_gdma_clear_status(&state_in->int_state,
                              R_GDMA_INTERRUPT_IN_DONE_MASK  |
                              R_GDMA_INTERRUPT_IN_SUC_EOF_MASK);
        esp_gdma_clear_status(&state_out->int_state,
                              R_GDMA_INTERRUPT_OUT_DONE_MASK |
                              R_GDMA_INTERRUPT_OUT_EOF_MASK  );

        /* Get highest 12 bits of the DRAM address */
        const uint32_t high = (ESP_GDMA_RAM_ADDR >> 20) << 20;

        /* TODO: in an inlink, when burst mode is enabled, size and buffer address must be word-aligned. */
        /* If a start was performed, the first descriptor address to process is in DMA_OUT_LINK_CHn register,
         * if a restart was performed, the first buffer is the `next` node of `desc_addr` register */
        uint32_t out_addr = high;
        uint32_t in_addr = high;

        if (out_start) {
            out_addr |= FIELD_EX32(state_out->link, GDMA_OUT_LINK, ADDR);
        } else {
            esp_gdma_get_restart_buffer(s, chan, ESP_GDMA_OUT_IDX, &out_addr);
        }

        if (in_start) {
            in_addr |= FIELD_EX32(state_in->link, GDMA_IN_LINK, ADDR);
        } else {
            esp_gdma_get_restart_buffer(s, chan, ESP_GDMA_IN_IDX, &in_addr);
        }

        /* Boolean to mark whether we need to check the owner for in and out buffers */
        const bool owner_check_out = FIELD_EX32(state_out->conf1, GDMA_OUT_CONF1, CHECK_OWNER);
        const bool owner_check_in  = FIELD_EX32(state_in->conf1, GDMA_IN_CONF1, CHECK_OWNER);
        /* Boolean to mark whether the transmit (out) buffers must have their owner bit cleared here */
        const bool clear_out = FIELD_EX32(state_out->conf0, GDMA_OUT_CONF0, AUTO_WRBACK);

        /* Pointer to the lists that will be browsed by the loop below */
        GdmaLinkedList out_list = { 0 };
        GdmaLinkedList in_list = { 0 };
        /* Boolean to mark whether a descriptor error occurred during the transfer */
        bool valid = true;
        bool error = false;

        /* Get the content of the descriptor located at guest address out_addr */
        valid = esp_gdma_read_descr(s, out_addr, &out_list);
        esp_gdma_push_descriptor(s, chan, ESP_GDMA_OUT_IDX, out_addr);

        /* Check that the address is valid. If the owner must be checked, make sure owner is the DMA controller.
         * On the real hardware, both in and out are checked at the same time, so in case of an error, both bits
         * are set. Replicate the same behavior here. */
        if ( !valid || (owner_check_out && !out_list.config.owner) ) {
            /* In case of an error, go directly to the next node */
            esp_gdma_set_status(&state_out->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
            error = true;
        }

        valid = esp_gdma_read_descr(s, in_addr, &in_list);
        esp_gdma_push_descriptor(s, chan, ESP_GDMA_IN_IDX, in_addr);

        if ( !valid || (owner_check_in && !in_list.config.owner) ) {
            esp_gdma_set_status(&state_in->int_state, R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK);
            error = true;
        }

        /* If any of the error bit has been set, return directly */
        if (error) {
            return;
        }

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
            valid = esp_gdma_read_guest(s, out_list.buf_addr + consumed, tmp_buffer, min);
            if (!valid) {
                esp_gdma_set_status(&state_out->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
                error = true;
            }
            valid = esp_gdma_write_guest(s, in_list.buf_addr + in_list.config.length, tmp_buffer, min);
            if (!valid) {
                esp_gdma_set_status(&state_in->int_state, R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK);
                error = true;
            }

            /* Update the number of bytes written to the "in" buffer */
            in_list.config.length += min;
            consumed += min;

            /* Even if we reached the end of the TX descriptor, we still have to update RX descriptors
             * and registers, use `exit_loop` instead of break or return */
            /* If we don't have any more bytes in the "out" buffer, we can skip to the next buffer */
            if (remaining == consumed) {
                /* Before jumping to the next node, clear the owner bit */
                if (clear_out) {
                    out_list.config.owner = 0;
                    /* Write back the modified descriptor, should always be valid */
                    valid = esp_gdma_write_descr(s, out_addr, &out_list);
                    assert(valid);
                }
                exit_loop = out_list.config.suc_eof ? true : false;

                const uint32_t next_addr = out_list.next_addr;
                valid = esp_gdma_next_list_node(s, chan, ESP_GDMA_OUT_IDX, &out_list);

                /* Only check the valid flag and the owner if we don't have to exit the loop*/
                if ( !exit_loop && (!valid || (owner_check_out && !out_list.config.owner)) ) {
                    esp_gdma_set_status(&state_out->int_state, R_GDMA_INTERRUPT_OUT_DSCR_ERR_MASK);
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
                valid = esp_gdma_write_descr(s, in_addr, &in_list);
                assert(valid);

                /* Check that we do have more "in" buffers, if that's not the case, raise an error..
                 * TODO: Check if the behavior is the same as Peripheral-to-Memory transfers, where
                 * this bit is only used to generate and interrupt. */
                if (!exit_loop && in_list.config.suc_eof) {
                    esp_gdma_set_status(&state_in->int_state, R_GDMA_INTERRUPT_IN_DSCR_EMPTY_MASK);
                    error = true;
                    break;
                }

                const uint32_t next_addr = in_list.next_addr;

                /* In the case where the transfer is finished, we should still "push" the next node
                 * to our descriptors stack, but we should not modify the structure itself as we will
                 * reset the owner and update the suc_eof flag */
                if (exit_loop) {
                    esp_gdma_push_descriptor(s, chan, ESP_GDMA_IN_IDX, next_addr);
                    break;
                }

                /* We have to continue the loop, so fetch the next node, it will also update the descriptors stack */
                valid = esp_gdma_next_list_node(s, chan, ESP_GDMA_IN_IDX, &in_list);

                /* Check the validity of the next node if we have to continue the loop (transfer finished) */
                if (!valid || (owner_check_in && !in_list.config.owner)) {
                    esp_gdma_set_status(&state_in->int_state, R_GDMA_INTERRUPT_IN_DSCR_ERR_MASK);
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
            valid = esp_gdma_write_descr(s, in_addr, &in_list);
            assert(valid);

            /* And store the EOF RX descriptor GUEST address in the correct register.
             * This can be used in the ISR to know which buffer has just been processed. */
            state_in->suc_eof_desc_addr = in_addr;

            /* Set the transfer as completed for both the IN and OUT link */
            esp_gdma_set_status(&state_in->int_state,
                                R_GDMA_INTERRUPT_IN_DONE_MASK  |
                                R_GDMA_INTERRUPT_IN_SUC_EOF_MASK);
            esp_gdma_set_status(&state_out->int_state,
                                R_GDMA_INTERRUPT_OUT_DONE_MASK |
                                R_GDMA_INTERRUPT_OUT_EOF_MASK);
        }

        g_free(tmp_buffer);
    }
}


/**
 * @brief Function called when a writable configuration register is being written to.
 *
 * @param s GDMA state structure
 * @param dir Channel direction: ESP_GDMA_IN_IDX or ESP_GDMA_OUT_IDX
 * @param chan Index of the channel
 * @param reg Register being written to in the block
 * @param value 32-bit value being written to the register
 */
static void esp_gdma_write_chan_conf(ESPGdmaState *state, uint32_t dir, uint32_t chan,
                                     DmaRegister reg, uint32_t value)
{
    DmaConfigState* s = &state->ch_conf[dir][chan];

    /* We will only support a subset of GDMA registers for now. To add support for more registers,
     * the following snippet can be update */
    uint32_t start_mask = 0;
    uint32_t restart_mask = 0;

    switch(reg) {

        /* No matter the channel and in/out direction, the registers are organized the same way,
         * so we can use the macros for any channel */
        case GDMA_CONF0_REG:
            /* Check the reset bit, call the reset function on negative edge */
            if (FIELD_EX32(value,  GDMA_IN_CONF0, RST) == 0 &&
                FIELD_EX32(s->conf0, GDMA_IN_CONF0, RST) != 0)
            {
                esp_gdma_reset_fifo(s);
            }
            /* Update the register before going further */
            s->conf0 = value;
            /* Check if memory transfer has just been enabled (only valid for IN channels) */
            if (dir == ESP_GDMA_IN_IDX &&
                FIELD_EX32(value,  GDMA_IN_CONF0, MEM_TRANS_EN))
            {
                esp_gdma_check_and_start_mem_transfer(state, chan);
            }
            break;

        case GDMA_LINK_REG:
            s->link = value;
            /* For IN and OUT, the START bit is not at the same offset, so we need to test both separately */
            start_mask   = (dir == ESP_GDMA_IN_IDX) ? R_GDMA_IN_LINK_START_MASK   : R_GDMA_OUT_LINK_START_MASK;
            restart_mask = (dir == ESP_GDMA_IN_IDX) ? R_GDMA_IN_LINK_RESTART_MASK : R_GDMA_OUT_LINK_RESTART_MASK;

            /* Check if any of the previous two bits has just been enabled */
            if ((value & start_mask) || (value & restart_mask))
            {
                esp_gdma_check_and_start_mem_transfer(state, chan);
            }
            break;

        case GDMA_CONF1_REG:
            s->conf1 = value;
            break;
        case GDMA_POP_REG:
            s->push_pop = value;
            break;
        case GDMA_PRIORITY_REG:
            s->priority = value;
            break;
        case GDMA_PERI_SEL_REG:
            s->peripheral = value;
            break;

        default:
            break;
    }
}


/**
 * @brief Write a virtual register of a channel. This function can be called by the child classes.
 */
void esp_gdma_write_chan_register(ESPGdmaState* s, uint32_t dir, uint32_t chan, DmaRegister reg, uint32_t value)
{
    ESPGdmaClass *class = ESP_GDMA_GET_CLASS(s);
    assert(s != NULL && chan < class->m_channel_count && dir < ESP_GDMA_CONF_COUNT);

    switch (reg) {
        /* Interrupt related */
        case GDMA_INT_RAW_REG:
        case GDMA_INT_ENA_REG:
        case GDMA_INT_CLR_REG:
            esp_gdma_write_int_state(&s->ch_conf[dir][chan].int_state, reg, value);
            break;

        /* Configuration related */
        case GDMA_CONF0_REG:
        case GDMA_CONF1_REG:
        case GDMA_POP_REG:
        case GDMA_LINK_REG:
        case GDMA_PRIORITY_REG:
        case GDMA_PERI_SEL_REG:
            esp_gdma_write_chan_conf(s, dir, chan, reg, value);
            break;

        default:
            /* RO registers or invalid register */
            break;
    }
}


void esp_gdma_write_register(ESPGdmaState* s, DmaRegister reg, uint32_t value)
{
    if (reg == GDMA_MISC_REG) {
        s->misc_conf = value;
    }
}


/**
 * @brief Read a virtual register of a channel. This function can be called by the child classes.
 */
uint64_t esp_gdma_read_chan_register(ESPGdmaState* state, uint32_t dir, uint32_t chan, DmaRegister reg)
{
    ESPGdmaClass *class = ESP_GDMA_GET_CLASS(state);
    assert(state != NULL && chan < class->m_channel_count && dir < ESP_GDMA_CONF_COUNT);
    /* In theory, we can simply cast the DmaConfigState structure into a `uint32_t` array, but let's make
     * it modular and not bound to any hardware representation. */
    const DmaConfigState* s = &state->ch_conf[dir][chan];

    switch (reg) {
        /* Interrupt related */
        case GDMA_INT_RAW_REG:          return s->int_state.raw;
        case GDMA_INT_ST_REG:           return s->int_state.st;
        case GDMA_INT_ENA_REG:          return s->int_state.ena;

        /* Configuration related */
        case GDMA_CONF0_REG:            return s->conf0;
        case GDMA_CONF1_REG:            return s->conf1;
        case GDMA_FIFO_ST_REG:          return s->status;
        case GDMA_POP_REG:              return s->push_pop;
        case GDMA_LINK_REG:             return s->link;
        case GDMA_STATE_REG:            return s->state;
        case GDMA_SUC_EOF_DESC_REG:     return s->suc_eof_desc_addr;
        case GDMA_ERR_EOF_DESC_REG:     return s->err_eof_desc_addr;
        case GDMA_DESC_ADDR_REG:        return s->desc_addr;
        case GDMA_BF0_DESC_ADDR_REG:    return s->bfr_desc_addr;
        case GDMA_BF1_DESC_ADDR_REG:    return s->bfr_bfr_desc_addr;
        case GDMA_PRIORITY_REG:         return s->priority;
        case GDMA_PERI_SEL_REG:         return s->peripheral;
        default:
            /* WO registers or invalid register */
            return 0;
    }
    return 0;
}


/**
 * @brief Read a virtual register that is NOT part of a GDMA channel.
 */
uint64_t esp_gdma_read_register(ESPGdmaState* s, DmaRegister reg)
{
    uint64_t r = 0;

    if (reg == GDMA_MISC_REG) {
        r = s->misc_conf;
    }

    return r;
}



static Property esp_gdma_properties[] = {
    DEFINE_PROP_LINK("soc_mr", ESPGdmaState, soc_mr, TYPE_MEMORY_REGION, MemoryRegion*),
    DEFINE_PROP_END_OF_LIST(),
};


static void esp_gdma_reset_hold(Object *obj, ResetType type)
{
    ESPGdmaState *s = ESP_GDMA(obj);
    ESPGdmaClass *klass = ESP_GDMA_GET_CLASS(obj);

    for (int dir = 0; dir < ESP_GDMA_CONF_COUNT; dir++) {
        for (int chan = 0; chan < klass->m_channel_count; chan++) {
            /* Backup IRQ since it's going to be erased by the `memset` */
            DmaConfigState* config = &s->ch_conf[dir][chan];
            const qemu_irq irq = config->int_state.irq;
            memset(config, 0, sizeof(DmaConfigState));
            /* Lower the IRQ and restore it in the configuration structure */
            qemu_irq_lower(irq);
            config->int_state.irq = irq;
            esp_gdma_reset_fifo(config);
        }
    }

    s->misc_conf = 0;
}


static void esp_gdma_realize(DeviceState *dev, Error **errp)
{
    ESPGdmaState *s = ESP_GDMA(dev);

    /* Make sure the DRAM MemoryRegion was set */
    assert(s->soc_mr != NULL);

    address_space_init(&s->dma_as, s->soc_mr, "esp.gdma");
}


static void esp_gdma_init(Object *obj)
{
    ESPGdmaState *s = ESP_GDMA(obj);
    ESPGdmaClass *klass = ESP_GDMA_GET_CLASS(obj);

    /* Make sure the number of channels passed by the child class is correct, use an abitrary limit */
    if (klass->m_channel_count == 0 || klass->m_channel_count > 16) {
        error_report("[GDMA] %s: invalid number of DMA channels (%zu)", __func__, klass->m_channel_count);
    }

    /* Initialize the DmaConfigState arrays */
    for (int dir = 0; dir < ESP_GDMA_CONF_COUNT; dir++) {
        s->ch_conf[dir] = g_malloc(sizeof(DmaConfigState) * klass->m_channel_count);
        if (s->ch_conf[dir] == NULL) {
            error_report("[GDMA] %s: could not allocate DmaConfigState", __func__);
        }
        const char* name = (dir == ESP_GDMA_OUT_IDX) ? ESP_GDMA_IRQ_OUT_NAME : ESP_GDMA_IRQ_IN_NAME;

        for (int chan = 0; chan < klass->m_channel_count; chan++) {
            qdev_init_gpio_out_named(DEVICE(obj), &s->ch_conf[dir][chan].int_state.irq, name, 1);
        }
    }

    esp_gdma_reset_hold(obj, RESET_TYPE_COLD);
}


static void esp_gdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = esp_gdma_reset_hold;
    dc->realize = esp_gdma_realize;
    device_class_set_props(dc, esp_gdma_properties);
}

static const TypeInfo esp_gdma_info = {
        .name = TYPE_ESP_GDMA,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(ESPGdmaState),
        .instance_init = esp_gdma_init,
        .class_init = esp_gdma_class_init,
        .abstract = true,
};

static void esp_gdma_register_types(void)
{
    type_register_static(&esp_gdma_info);
}

type_init(esp_gdma_register_types)
