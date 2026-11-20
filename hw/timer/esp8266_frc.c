/*
 * Copyright (c) 2014, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ESP8266 FRC1/FRC2 timers (0x60000600; register layout as in esp-open-rtos
 * timer_regs.h), based on Max Filippov's ESP8266 model from OSLL/qemu-xtensa
 * (xtensa-esp8266 branch).
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/timer/esp8266_frc.h"

enum {
    ESP8266_FRC_LOAD,
    ESP8266_FRC_COUNT,
    ESP8266_FRC_CTRL,
    ESP8266_FRC_STATUS,     /* write: clear interrupt */
    ESP8266_FRC_ALARM,      /* FRC2 only */
};

#define ESP8266_FRC_CTRL_LEVEL      BIT(0)
#define ESP8266_FRC_CTRL_DIV_SHIFT  2
#define ESP8266_FRC_CTRL_RELOAD     BIT(6)
#define ESP8266_FRC_CTRL_RUN        BIT(7)
#define ESP8266_FRC_CTRL_INT        BIT(8)

#define ESP8266_FRC1_MASK 0x7fffff

static uint32_t esp8266_frc_hz(Esp8266FrcState *s, int n)
{
    static const unsigned shift[] = { 0, 4, 8, 8 };

    return ESP8266_APB_HZ >>
           shift[(s->ctrl[n] >> ESP8266_FRC_CTRL_DIV_SHIFT) & 3];
}

static uint64_t esp8266_frc_ticks(Esp8266FrcState *s, int n, int64_t now)
{
    if (!(s->ctrl[n] & ESP8266_FRC_CTRL_RUN)) {
        return 0;
    }
    return muldiv64(now - s->base_ns[n], esp8266_frc_hz(s, n),
                    NANOSECONDS_PER_SECOND);
}

static uint32_t esp8266_frc_count(Esp8266FrcState *s, int n, int64_t now)
{
    uint64_t ticks = esp8266_frc_ticks(s, n, now);

    if (n == 1) {
        return s->base_count[1] + (uint32_t)ticks;     /* FRC2 counts up */
    }
    /* FRC1 counts down to 0, then reloads (auto-reload) or stays at 0. */
    if (ticks <= s->base_count[0]) {
        return s->base_count[0] - ticks;
    }
    if ((s->ctrl[0] & ESP8266_FRC_CTRL_RELOAD) && s->load[0]) {
        ticks -= s->base_count[0];
        return s->load[0] - (ticks % s->load[0]);
    }
    return 0;
}

static void esp8266_frc_rearm(Esp8266FrcState *s, int n)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t count = esp8266_frc_count(s, n, now);
    uint64_t ticks;

    s->base_count[n] = count;
    s->base_ns[n] = now;
    if (!(s->ctrl[n] & ESP8266_FRC_CTRL_RUN)) {
        timer_del(s->timer[n]);
        return;
    }
    if (n == 1) {
        ticks = (uint32_t)(s->alarm - count);
        if (!ticks) {
            ticks = 1ull << 32;
        }
    } else {
        ticks = count ? count : s->load[0];
        if (!ticks) {
            timer_del(s->timer[n]);
            return;
        }
    }
    timer_mod(s->timer[n],
              now + muldiv64(ticks, NANOSECONDS_PER_SECOND,
                             esp8266_frc_hz(s, n)));
}

static void esp8266_frc_fire(Esp8266FrcState *s, int n)
{
    s->ctrl[n] |= ESP8266_FRC_CTRL_INT;
    if (n == 0 && s->nmi_route) {
        qemu_irq_pulse(s->nmi);
    } else if (s->ctrl[n] & ESP8266_FRC_CTRL_LEVEL) {
        qemu_irq_raise(s->irq[n]);
    } else {
        qemu_irq_pulse(s->irq[n]);
    }
    esp8266_frc_rearm(s, n);
}

static void esp8266_frc1_cb(void *opaque)
{
    esp8266_frc_fire(opaque, 0);
}

static void esp8266_frc2_cb(void *opaque)
{
    esp8266_frc_fire(opaque, 1);
}

static uint64_t esp8266_frc_read(void *opaque, hwaddr addr, unsigned size)
{
    Esp8266FrcState *s = opaque;
    int n = addr >= 0x20;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    switch ((addr & 0x1f) / 4) {
    case ESP8266_FRC_LOAD:
        return s->load[n];
    case ESP8266_FRC_COUNT:
        return esp8266_frc_count(s, n, now);
    case ESP8266_FRC_CTRL:
        return s->ctrl[n];
    case ESP8266_FRC_ALARM:
        return n ? s->alarm : 0;
    default:
        return 0;
    }
}

static void esp8266_frc_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    Esp8266FrcState *s = opaque;
    int n = addr >= 0x20;

    switch ((addr & 0x1f) / 4) {
    case ESP8266_FRC_LOAD:
        s->load[n] = n ? val : val & ESP8266_FRC1_MASK;
        esp8266_frc_rearm(s, n);
        s->base_count[n] = s->load[n];  /* loading restarts the count */
        esp8266_frc_rearm(s, n);
        break;
    case ESP8266_FRC_CTRL:
        esp8266_frc_rearm(s, n);        /* freeze the count at the old rate */
        s->ctrl[n] = (s->ctrl[n] & ESP8266_FRC_CTRL_INT) |
                     (val & ~ESP8266_FRC_CTRL_INT & 0xff);
        s->base_ns[n] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        esp8266_frc_rearm(s, n);
        break;
    case ESP8266_FRC_STATUS:
        s->ctrl[n] &= ~ESP8266_FRC_CTRL_INT;
        qemu_irq_lower(s->irq[n]);
        break;
    case ESP8266_FRC_ALARM:
        if (n) {
            s->alarm = val;
            esp8266_frc_rearm(s, 1);
        }
        break;
    }
}

static const MemoryRegionOps esp8266_frc_ops = {
    .read = esp8266_frc_read,
    .write = esp8266_frc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/* DPORT 0x00 bit 0: FRC1 goes to the NMI instead of its interrupt line. */
static void esp8266_frc_nmi_route(void *opaque, int n, int level)
{
    Esp8266FrcState *s = opaque;

    s->nmi_route = level;
}

static void esp8266_frc_reset_hold(Object *obj, ResetType type)
{
    Esp8266FrcState *s = ESP8266_FRC(obj);

    for (int n = 0; n < 2; n++) {
        timer_del(s->timer[n]);
        s->load[n] = s->ctrl[n] = s->base_count[n] = 0;
        s->base_ns[n] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        qemu_irq_lower(s->irq[n]);
    }
    s->alarm = 0;
}

static void esp8266_frc_init(Object *obj)
{
    Esp8266FrcState *s = ESP8266_FRC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL, esp8266_frc1_cb, s);
    s->timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL, esp8266_frc2_cb, s);
    memory_region_init_io(&s->iomem, obj, &esp8266_frc_ops, s,
                          TYPE_ESP8266_FRC, 0x40);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq[0]);
    sysbus_init_irq(sbd, &s->irq[1]);
    sysbus_init_irq(sbd, &s->nmi);
    qdev_init_gpio_in_named(DEVICE(obj), esp8266_frc_nmi_route,
                            ESP8266_FRC_NMI_ROUTE_GPIO, 1);
}

static void esp8266_frc_finalize(Object *obj)
{
    Esp8266FrcState *s = ESP8266_FRC(obj);

    timer_free(s->timer[0]);
    timer_free(s->timer[1]);
}

static const VMStateDescription vmstate_esp8266_frc = {
    .name = TYPE_ESP8266_FRC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR_ARRAY(timer, Esp8266FrcState, 2),
        VMSTATE_UINT32_ARRAY(load, Esp8266FrcState, 2),
        VMSTATE_UINT32_ARRAY(ctrl, Esp8266FrcState, 2),
        VMSTATE_UINT32(alarm, Esp8266FrcState),
        VMSTATE_UINT32_ARRAY(base_count, Esp8266FrcState, 2),
        VMSTATE_INT64_ARRAY(base_ns, Esp8266FrcState, 2),
        VMSTATE_BOOL(nmi_route, Esp8266FrcState),
        VMSTATE_END_OF_LIST()
    },
};

static void esp8266_frc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_esp8266_frc;
    rc->phases.hold = esp8266_frc_reset_hold;
}

static const TypeInfo esp8266_frc_info = {
    .name = TYPE_ESP8266_FRC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp8266FrcState),
    .instance_init = esp8266_frc_init,
    .instance_finalize = esp8266_frc_finalize,
    .class_init = esp8266_frc_class_init,
};

static void esp8266_frc_register_types(void)
{
    type_register_static(&esp8266_frc_info);
}

type_init(esp8266_frc_register_types)
