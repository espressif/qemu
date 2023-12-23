#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"


#define TYPE_ESP_RGB "display.esp.rgb"
#define ESP_RGB(obj)           OBJECT_CHECK(ESPRgbState, (obj), TYPE_ESP_RGB)
#define ESP_RGB_GET_CLASS(obj) OBJECT_GET_CLASS(ESPRgbState, obj, TYPE_ESP_RGB)
#define ESP_RGB_CLASS(klass)   OBJECT_CLASS_CHECK(ESPRgbState, klass, TYPE_ESP_RGB)


#define ESP_RGB_MAX_WIDTH   (800)
#define ESP_RGB_MAX_HEIGHT  (600)
#define ESP_RGB_MAX_VRAM_SIZE   (ESP_RGB_MAX_WIDTH * ESP_RGB_MAX_HEIGHT * 4)

#define DEFAULT_BPP (BPP_32)

typedef enum {
    BPP_16 = 16,
    BPP_32 = 32
} BppEnum;

typedef struct ESPRgbState {
    SysBusDevice parent_obj;

    QemuConsole *con;
    MemoryRegion iomem;
    MemoryRegion vram;
    AddressSpace vram_as;
    /* Internal RAM memory region */
    MemoryRegion* intram;
    AddressSpace intram_as;

    /* Window size */
    uint32_t width;
    uint32_t height;
    bool do_update_surface;

    /* Update area */
    bool update_area;
    uint32_t from_x;
    uint32_t from_y;
    uint32_t to_x;
    uint32_t to_y;
    uint32_t color_content;

    /* BPP */
    BppEnum bpp;
} ESPRgbState;

#define ESP_RGB_IO_SIZE (A_RGB_UPDATE_STATUS + 8)

REG32(RGB_VERSION, 0x00)
    FIELD(RGB_VERSION, MAJOR, 16, 16)
    FIELD(RGB_VERSION, MINOR, 0, 16)

/* Used to resize the window */
REG32(RGB_WIN_SIZE, 0x04)
    FIELD(RGB_WIN_SIZE, WIDTH, 16, 16)
    FIELD(RGB_WIN_SIZE, HEIGHT, 0, 16)

/* Used to define the area to update on next window update */
REG32(RGB_UPDATE_FROM, 0x08)
    FIELD(RGB_UPDATE_FROM, X, 16, 16)
    FIELD(RGB_UPDATE_FROM, Y, 0, 16)

REG32(RGB_UPDATE_TO, 0x0c)
    FIELD(RGB_UPDATE_TO, X, 16, 16)
    FIELD(RGB_UPDATE_TO, Y, 0, 16)

REG32(RGB_UPDATE_CONTENT, 0x10)

REG32(RGB_UPDATE_STATUS, 0x14)
    /* When set, only the area described above will be rendered
     * on next window update. When not set, the window is not updated.
     * Automatically cleared by the hardware after window update. */
    FIELD(RGB_UPDATE_STATUS, ENA, 0, 1)

REG32(RGB_BPP_VALUE, 0x18)