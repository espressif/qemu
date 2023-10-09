#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_phya.h"

static uint64_t esp32_phya_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t r = 0;
    Esp32PhyaState *s = ESP32_PHYA(opaque);
    r=s->mem[addr/4];
    return r;
}

static void esp32_phya_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size) {
  Esp32PhyaState *s = ESP32_PHYA(opaque);
  s->mem[addr/4]=(uint32_t)value;
}

static const MemoryRegionOps esp32_phya_ops = {
    .read =  esp32_phya_read,
    .write = esp32_phya_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_phya_init(Object *obj)
{
    Esp32PhyaState *s = ESP32_PHYA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_phya_ops, s,
                          TYPE_ESP32_PHYA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    memset(s->mem,0,sizeof(s->mem));
}


static const TypeInfo esp32_phya_info = {
    .name = TYPE_ESP32_PHYA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32PhyaState),
    .instance_init = esp32_phya_init,
};

static void esp32_phya_register_types(void)
{
    type_register_static(&esp32_phya_info);
}

type_init(esp32_phya_register_types)
