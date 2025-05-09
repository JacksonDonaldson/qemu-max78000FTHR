/*
 * MAX78000 Instruction Cache
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/max78000_icc.h"


static uint64_t max78000_icc_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max78000IccState *s = opaque;
    switch (addr) {
        case ICC_INFO:{
            return s->info;
        }
        case ICC_SZ:{
            return s->sz;
        }
        case ICC_CTRL:{
            return s->ctrl;
        }
        case ICC_INVALIDATE:{
            return s->invalidate;
        }
        default:{
            return 0;
        }
    }
}

static void max78000_icc_write(void *opaque, hwaddr addr,
                    uint64_t val64, unsigned int size)
{
    Max78000IccState *s = opaque;

    switch (addr) {
        case ICC_CTRL:{
        s->ctrl = 0x10000 + (val64 & 1);
        break;
        }
        case ICC_INVALIDATE:{
        s->ctrl = s->ctrl | 0x80;
        }
    }
}

static const MemoryRegionOps stm32f4xx_exti_ops = {
    .read = max78000_icc_read,
    .write = max78000_icc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void max78000_icc_init(Object *obj)
{
    Max78000IccState *s = MAX78000_ICC(obj);
    s->info = 0;
    s->sz = 0x10000010;
    s->ctrl = 0x10000;
    s->invalidate = 0;


    memory_region_init_io(&s->mmio, obj, &stm32f4xx_exti_ops, s,
                        TYPE_MAX78000_ICC, 0x800);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

}

static const TypeInfo max78000_icc_info = {
    .name          = TYPE_MAX78000_ICC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000IccState),
    .instance_init = max78000_icc_init,
};

static void max78000_icc_register_types(void)
{
    type_register_static(&max78000_icc_info);
}

type_init(max78000_icc_register_types)
