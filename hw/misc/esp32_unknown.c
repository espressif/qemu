/* ESP32 unknown peripheral handler
*/

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/boards.h"
#include "hw/misc/esp32_unknown.h"
#include "hw/misc/esp32_reg.h"

static uint64_t esp32_unknown_read(void *opaque, hwaddr addr, unsigned int size)
{   
    addr += 0x3ff00000;
    uint64_t r = 0;
    switch (addr) {
        case (DR_REG_ANA_BASE + 0x04c): { // used in ram_txdc_cal_v70, esp32.analog region
            r = 0x1000000;
            break;
        }
        case 0x3ff4607c: { // used in ram_iq_est_enable, reserved region
            r = 0xffffffff;
            break;
        }

        default: {
            printf("unknown read for %016lX\n", addr);
            break;
        }
    }
    // Esp32UnknownState *s = ESP32_UNKNOWN(opaque);
    return r;
}

static void esp32_unknown_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    addr += 0x3ff00000;
    // Esp32UnknownState *s = ESP32_UNKNOWN(opaque);
    printf("unknown write for %016lX, setting to %ld (size=%d)\n", addr, value, size);
    
}

static const MemoryRegionOps esp32_unknown_ops = {
    .read =  esp32_unknown_read,
    .write = esp32_unknown_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_unknown_init(Object *obj)
{
    Esp32UnknownState *s = ESP32_UNKNOWN(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_unknown_ops, s,
                          TYPE_ESP32_UNKNOWN, 0x80000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const TypeInfo esp32_unknown_info = {
    .name = TYPE_ESP32_UNKNOWN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32UnknownState),
    .instance_init = esp32_unknown_init,
};

static void esp32_unknown_register_types(void)
{
    type_register_static(&esp32_unknown_info);
}

type_init(esp32_unknown_register_types)
