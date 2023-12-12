/*
 * ESP Display RGB emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This implementation overrides the ESP32 UARt one, check it out first.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/display/esp_rgb.h"
#include "ui/console.h"
#include "qemu/error-report.h"
#include "sysemu/dma.h"

#define RGB_WARNING 1
#define RGB_DEBUG   0

#define RGB_VERSION_MAJOR 0
#define RGB_VERSION_MINOR 1

static uint64_t esp_rgb_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESPRgbState *s = ESP_RGB(opaque);
    uint32_t r = 0;

    /* Only accept 32-bit transactions */
    if (size != sizeof(uint32_t)) {
        return r;
    }

    switch(addr) {
        case A_RGB_VERSION:
            r = FIELD_DP32(r, RGB_VERSION, MAJOR, RGB_VERSION_MAJOR);
            r = FIELD_DP32(r, RGB_VERSION, MINOR, RGB_VERSION_MINOR);
            break;

        case A_RGB_WIN_SIZE:
            r = FIELD_DP32(r, RGB_WIN_SIZE, WIDTH, s->width);
            r = FIELD_DP32(r, RGB_WIN_SIZE, HEIGHT, s->height);
            break;

        case A_RGB_UPDATE_FROM:
            r = FIELD_DP32(r, RGB_UPDATE_FROM, X, s->from_x);
            r = FIELD_DP32(r, RGB_UPDATE_FROM, Y, s->from_y);
            break;

        case A_RGB_UPDATE_TO:
            r = FIELD_DP32(r, RGB_UPDATE_TO, X, s->to_x);
            r = FIELD_DP32(r, RGB_UPDATE_TO, Y, s->to_y);
            break;

        case A_RGB_UPDATE_CONTENT:
            r = s->color_content;
            break;

        case A_RGB_UPDATE_STATUS:
            r = s->update_area;
            break;

        default:
#if RGB_WARNING
            warn_report("[ESP RGB] Unsupported read to 0x%lx", (unsigned long) addr);
#endif
            break;
    }

#if RGB_DEBUG
    info_report("[ESP RGB] Reading 0x%lx (0x%x)", addr, r);
#endif

    return r;
}


static void esp_rgb_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESPRgbState *s = ESP_RGB(opaque);
    uint32_t width = 0;
    uint32_t height = 0;

    /* Only accept 32-bit transactions */
    if (size != sizeof(uint32_t)) {
        return;
    }

#if RGB_DEBUG
    info_report("[ESP RGB] Writing 0x%lx = %08lx", addr, value);
#endif

    switch(addr) {
        case A_RGB_WIN_SIZE:
            width = FIELD_EX32(value, RGB_WIN_SIZE, WIDTH);
            height = FIELD_EX32(value, RGB_WIN_SIZE, HEIGHT);

            /* Check the bounds of the new window size */
            s->width = MIN(width, ESP_RGB_MAX_WIDTH);
            s->height = MIN(height, ESP_RGB_MAX_HEIGHT);
            /* Make sure it's bigger than 10x10 */
            s->width = MAX(s->width, 10);
            s->height = MAX(s->height, 10);

            /* Update the window size */
            s->do_update_size = true;
            break;

        case A_RGB_UPDATE_STATUS:
            s->update_area = FIELD_EX32(value, RGB_UPDATE_STATUS, ENA) != 0;
            break;

        case A_RGB_UPDATE_FROM:
            s->from_x = FIELD_EX32(value, RGB_UPDATE_FROM, X);
            s->from_y = FIELD_EX32(value, RGB_UPDATE_FROM, Y);
            break;

        case A_RGB_UPDATE_TO:
            s->to_x = FIELD_EX32(value, RGB_UPDATE_TO, X);
            s->to_y = FIELD_EX32(value, RGB_UPDATE_TO, Y);
            break;

        case A_RGB_UPDATE_CONTENT:
            s->color_content = (uint32_t) value;
            break;

        default:
#if RGB_WARNING
            warn_report("[ESP RGB] Unsupported write to 0x%lx (%08lx)", (unsigned long) addr, (unsigned long) value);
#endif
            break;
    }

}


static void rgb_update(void* opaque)
{
    ESPRgbState* s = (ESPRgbState*) opaque;

    if (s->con && s->do_update_size) {
        qemu_console_resize(s->con, s->width, s->height);
        s->do_update_size = false;
    }

    if (s->con && s->update_area) {
        uint32_t src = s->color_content;
        AddressSpace* src_as = NULL;

        /* Since we are in a 32bpp configuration, it's enough to cast the framebuffer
         * as a uint32_t pointer */
        uint32_t* data = surface_data(qemu_console_surface(s->con));

        /* Width and height of the area to update */
        const int width = s->to_x - s->from_x;
        const int height = s->to_y - s->from_y;
        const int bytes_per_pixel = sizeof(uint32_t);
        const int total_bytes = width * height * bytes_per_pixel;

        if (address_space_access_valid(&s->vram_as, src, total_bytes, false, MEMTXATTRS_UNSPECIFIED)) {
            src_as = &s->vram_as;
        } else if (address_space_access_valid(&s->intram_as, src, total_bytes, false, MEMTXATTRS_UNSPECIFIED)) {
            src_as = &s->intram_as;
        } else {
            s->update_area = false;
#if RGB_WARNING
            warn_report("[ESP RGB] Invalid color content address or length");
#endif
            return;
        }

        /* Only perform the copy if the area is valid */
        if (width > 0 && height > 0 &&
            (s->from_x + width) <= s->width && (s->from_y + height) <= s->height) {

            uint32_t* dest = &data[s->from_y * s->width + s->from_x];

            /* Copy the pixels to the framebuffer */
            for (int i = 0; i < height; i++) {
                dma_memory_read(src_as, src, dest, width * bytes_per_pixel, MEMTXATTRS_UNSPECIFIED);
                /* Go to the next line in the destination */
                dest += s->width;
                /* Same goes for the source */
                src += width * bytes_per_pixel;
            }

            dpy_gfx_update(s->con, s->from_x, s->from_y, width, height);
        }
#if RGB_WARNING
        else {
            warn_report("[ESP RGB] Invalid drawing area");
        }
#endif

        /* Automatically clear the update flag, the guest can re-use the given color_content buffer.
         * It must set it again to trigger another update. */
        s->update_area = false;
    }
}


static void rgb_invalidate(void *opaque)
{
    ESPRgbState* s = (ESPRgbState*) opaque;

    if (s->con) {
        uint32_t* data = surface_data(qemu_console_surface(s->con));

        /* On invalidate, reset the display */
        memset(data, 0, s->width * s->height * 4);
    }
}


static const GraphicHwOps fb_ops = {
    .invalidate = rgb_invalidate,
    .gfx_update  = rgb_update
};


static void esp_rgb_realize(DeviceState *dev, Error **errp)
{
    ESPRgbState* s = ESP_RGB(dev);

    assert(s->intram != NULL);
    /* Create an address space for internal RAM so that we can read data from it on GUI update */
    address_space_init(&s->intram_as, s->intram, "esp32c3.rgb.intram_as");
}


static const MemoryRegionOps esp_rgb_ops = {
    .read =  esp_rgb_read,
    .write = esp_rgb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void esp_rgb_init(Object *obj)
{
    ESPRgbState* s = ESP_RGB(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp_rgb_ops, s,
                          TYPE_ESP_RGB, ESP_RGB_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Default window size */
    s->width = ESP_RGB_MAX_WIDTH;
    s->height = ESP_RGB_MAX_HEIGHT;
    s->update_area = false;

    if (s->con == NULL) {
        s->con = graphic_console_init(DEVICE(s), 0, &fb_ops, s);
        qemu_console_resize(s->con, s->width, s->height);
        uint32_t* data = surface_data(qemu_console_surface(s->con));
        /* Initialize the window to black */
        memset(data, 0, ESP_RGB_MAX_VRAM_SIZE);
    }

    /* Create a memory region that can be used as a framebuffer by the guest */
    memory_region_init_ram(&s->vram, OBJECT(s), "esp32c3-rgb-vram", ESP_RGB_MAX_VRAM_SIZE, &error_abort);

    /* Create an AddressSpace out of the MemoryRegion to be able to perform DMA */
    address_space_init(&s->vram_as, &s->vram, "esp32c3.rgb.vram_as");
}


static void esp_rgb_reset(DeviceState *dev)
{
    ESPRgbState* s = ESP_RGB(dev);

    /* Default window size */
    s->width = ESP_RGB_MAX_WIDTH;
    s->height = ESP_RGB_MAX_HEIGHT;
    s->update_area = false;
    s->from_x = 0;
    s->from_y = 0;
    s->to_x = 0;
    s->to_y = 0;
}


static void esp_rgb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp_rgb_reset;
    dc->realize = esp_rgb_realize;
}

static const TypeInfo esp_rgb_info = {
    .name = TYPE_ESP_RGB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESPRgbState),
    .instance_init = esp_rgb_init,
    .class_init = esp_rgb_class_init,
};

static void esp_rgb_register_types(void)
{
    type_register_static(&esp_rgb_info);
}

type_init(esp_rgb_register_types)
