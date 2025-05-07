/*
 * MAX78000 True Random Number Generator
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
#include "hw/misc/max78000_trng.h"
#include "crypto/random.h"

static uint64_t max78000_trng_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    uint8_t buf[4];
    Error *errp = NULL;

    Max78000TrngState *s = opaque;
    switch (addr) {
        case CTRL:{
            return s->ctrl;
        }
        case STATUS:{
            return 1;
        }
        case DATA:{
            qcrypto_random_bytes(buf, 4, &errp);
            return *(uint32_t *)buf;
        }
        default:{
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                HWADDR_PRIx "\n", TYPE_MAX78000_TRNG, __func__, addr);
            break;
        }
    }
    return 0;
}

static void max78000_trng_write(void *opaque, hwaddr addr,
                    uint64_t val64, unsigned int size)
{
    Max78000TrngState *s = opaque;
    uint32_t val = val64;
    switch (addr) {
        case CTRL:{
            /* TODO: implement AES keygen */
            s->ctrl = val;
            if (val & RND_IE) {
                qemu_set_irq(s->irq, 1);
            } else{
                qemu_set_irq(s->irq, 0);
            }
            break;
        }
        default:{
            qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                HWADDR_PRIx "\n", TYPE_MAX78000_TRNG, __func__, addr);
            break;
        }

    }
}

static void max78000_trng_reset_hold(Object *obj, ResetType type)
{
    Max78000TrngState *s = MAX78000_TRNG(obj);
    s->ctrl = 0;
    s->status = 0;
    s->data = 0;
}

static const MemoryRegionOps max78000_trng_ops = {
    .read = max78000_trng_read,
    .write = max78000_trng_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void max78000_trng_init(Object *obj)
{
    Max78000TrngState *s = MAX78000_TRNG(obj);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &max78000_trng_ops, s,
                        TYPE_MAX78000_TRNG, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

}

static void max78000_trng_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max78000_trng_reset_hold;

}

static const TypeInfo max78000_trng_info = {
    .name          = TYPE_MAX78000_TRNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000TrngState),
    .instance_init = max78000_trng_init,
    .class_init    = max78000_trng_class_init,
};

static void max78000_trng_register_types(void)
{
    type_register_static(&max78000_trng_info);
}

type_init(max78000_trng_register_types)
