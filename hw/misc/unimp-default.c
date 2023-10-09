/* "Unimplemented" default device
 *
 * This is a dummy device which accepts and logs all accesses.
 * It's useful for stubbing out regions of an SoC or board
 * map which correspond to devices that have not yet been
 * implemented. This is often sufficient to placate initial
 * guest device driver probing such that the system will
 * come up.
 *
 * Copyright Linaro Limited, 2017
 * Written by Peter Maydell
 * Modified by redfast00 2023
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/unimp-default.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

static uint64_t unimp_default_read(void *opaque, hwaddr offset, unsigned size)
{
    UnimplementedDefaultDeviceState *s = UNIMPLEMENTED_DEFAULT_DEVICE(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented default device read  "
                  "(size %d, offset 0x%0*" HWADDR_PRIx ")\n",
                  s->name, size, s->offset_fmt_width, offset);
    return s->default_value;
}

static void unimp_default_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    UnimplementedDefaultDeviceState *s = UNIMPLEMENTED_DEFAULT_DEVICE(opaque);

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented default device write "
                  "(size %d, offset 0x%0*" HWADDR_PRIx
                  ", value 0x%0*" PRIx64 ")\n",
                  s->name, size, s->offset_fmt_width, offset, size << 1, value);
}

static const MemoryRegionOps unimp_default_ops = {
    .read = unimp_default_read,
    .write = unimp_default_write,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void unimp_default_realize(DeviceState *dev, Error **errp)
{
    UnimplementedDefaultDeviceState *s = UNIMPLEMENTED_DEFAULT_DEVICE(dev);

    if (s->size == 0) {
        error_setg(errp, "property 'size' not specified or zero");
        return;
    }

    if (s->name == NULL) {
        error_setg(errp, "property 'name' not specified");
        return;
    }

    s->offset_fmt_width = DIV_ROUND_UP(64 - clz64(s->size - 1), 4);

    memory_region_init_io(&s->iomem, OBJECT(s), &unimp_default_ops, s,
                          s->name, s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static Property unimp_default_properties[] = {
    DEFINE_PROP_UINT64("size", UnimplementedDefaultDeviceState, size, 0),
    DEFINE_PROP_STRING("name", UnimplementedDefaultDeviceState, name),
    DEFINE_PROP_UINT64("default_value", UnimplementedDefaultDeviceState, default_value, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void unimp_default_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = unimp_default_realize;
    device_class_set_props(dc, unimp_default_properties);
}

static const TypeInfo unimp_default_info = {
    .name = TYPE_UNIMPLEMENTED_DEFAULT_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UnimplementedDefaultDeviceState),
    .class_init = unimp_default_class_init,
};

static void unimp_default_register_types(void)
{
    type_register_static(&unimp_default_info);
}

type_init(unimp_default_register_types)
