/*
 * MAX78000 Global Control Registers
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "system/runstate.h"
#include "migration/vmstate.h"
#include "hw/misc/max78000_gcr.h"

static void max78000_gcr_reset(DeviceState *dev)
{
    Max78000GcrState *s = MAX78000_GCR(dev);
    s->sysctrl = 0x21002;
    s->rst0 = 0;
    /* All clocks are always ready */
    s->clkctrl = 0x3e140008;
    s->pm = 0x3f000;
    s->pclkdiv = 0;
    s->pclkdis0 = 0xffffffff;
    s->memctrl = 0x5;
    s->memz = 0;
    s->sysst = 0;
    s->rst1 = 0;
    s->pckdis1 = 0xffffffff;
    s->eventen = 0;
    s->revision = 0xa1;
    s->sysie = 0;
    s->eccerr = 0;
    s->ecced = 0;
    s->eccie = 0;
    s->eccaddr = 0;
}

static uint64_t max78000_gcr_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Max78000GcrState *s = opaque;

    switch (addr) {
        case SYSCTRL:{
            return s->sysctrl;
        }
        case RST0:{
            return s->rst0;
        }
        case CLKCTRL:{
            return s->clkctrl;
        }
        case PM:{
            return s->pm;
        }
        case PCLKDIV:{
            return s->pclkdiv;
        }
        case PCLKDIS0:{
            return s->pclkdis0;
        }
        case MEMCTRL:{
            return s->memctrl;
        }
        case MEMZ:{
            return s->memz;
        }
        case SYSST:{
            return s->sysst;
        }
        case RST1:{
            return s->rst1;
        }
        case PCKDIS1:{
            return s->pckdis1;
        }
        case EVENTEN:{
            return s->eventen;
        }
        case REVISION:{
            return s->revision;
        }
        case SYSIE:{
            return s->sysie;
        }
        case ECCERR:{
            return s->eccerr;
        }
        case ECCED:{
            return s->ecced;
        }
        case ECCIE:{
            return s->eccie;
        }
        case ECCADDR:{
            return s->eccaddr;
        }
        default:{
            return 0;
        }
    }
}

static void max78000_gcr_reset_device(const char *device_name)
{
    DeviceState *dev = qdev_find_recursive(sysbus_get_default(), device_name);
    if (dev) {
        device_cold_reset(dev);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "no device %s for reset\n", device_name);
    }
}

static void max78000_gcr_write(void *opaque, hwaddr addr,
                       uint64_t val64, unsigned int size)
{
    Max78000GcrState *s = opaque;
    uint32_t val = val64;
    uint8_t zero[0xc000] = {0};
    switch (addr) {
        case SYSCTRL:{
            /* Checksum calculations always pass immediately */
            s->sysctrl = (val & 0x30000) | 0x1002;
            break;
        }
        case RST0:{
            if (val & SYSTEM_RESET) {
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            }
            if (val & PERIPHERAL_RESET) {
                /*
                 * Peripheral reset resets all peripherals. The CPU
                 * retains its state. The GPIO, watchdog timers, AoD,
                 * RAM retention, and general control registers (GCR),
                 * including the clock configuration, are unaffected.
                 */
                val = UART2_RESET | UART1_RESET | UART0_RESET |
                      ADC_RESET | CNN_RESET | TRNG_RESET |
                      RTC_RESET | I2C0_RESET | SPI1_RESET |
                      TMR3_RESET | TMR2_RESET | TMR1_RESET |
                      TMR0_RESET | WDT0_RESET | DMA_RESET;
            }
            if (val & SOFT_RESET) {
                /* Soft reset also resets GPIO */
                val = UART2_RESET | UART1_RESET | UART0_RESET |
                      ADC_RESET | CNN_RESET | TRNG_RESET |
                      RTC_RESET | I2C0_RESET | SPI1_RESET |
                      TMR3_RESET | TMR2_RESET | TMR1_RESET |
                      TMR0_RESET | GPIO1_RESET | GPIO0_RESET |
                      DMA_RESET;
            }
            if (val & UART2_RESET) {
                max78000_gcr_reset_device("uart2");
            }
            if (val & UART1_RESET) {
                max78000_gcr_reset_device("uart1");
            }
            if (val & UART0_RESET) {
                max78000_gcr_reset_device("uart0");
            }
            /* TODO: As other devices are implemented, add them here */
            break;
        }
        case CLKCTRL:{
            s->clkctrl = val | SYSCLK_RDY;
            break;
        }
        case PM:{
            s->pm = val;
            break;
        }
        case PCLKDIV:{
            s->pclkdiv = val;
            break;
        }
        case PCLKDIS0:{
            s->pclkdis0 = val;
            break;
        }
        case MEMCTRL:{
            s->memctrl = val;
            break;
        }
        case MEMZ:{
            if (val & ram0) {
                cpu_physical_memory_write(SYSRAM0_START, zero, 0x8000);
            }
            if (val & ram1) {
                cpu_physical_memory_write(SYSRAM1_START, zero, 0x8000);
            }
            if (val & ram2) {
                cpu_physical_memory_write(SYSRAM2_START, zero, 0xC000);
            }
            if (val & ram3) {
                cpu_physical_memory_write(SYSRAM3_START, zero, 0x4000);
            }
            break;
        }
        case SYSST:{
            s->sysst = val;
            break;
        }
        case RST1:{
            /* TODO: As other devices are implemented, add them here */
            s->rst1 = val;
            break;
        }
        case PCKDIS1:{
            s->pckdis1 = val;
            break;
        }
        case EVENTEN:{
            s->eventen = val;
            break;
        }
        case REVISION:{
            s->revision = val;
            break;
        }
        case SYSIE:{
            s->sysie = val;
            break;
        }
        case ECCERR:{
            s->eccerr = val;
            break;
        }
        case ECCED:{
            s->ecced = val;
            break;
        }
        case ECCIE:{
            s->eccie = val;
            break;
        }
        case ECCADDR:{
            s->eccaddr = val;
            break;
        }
        default:{
            break;
        }
    }
}

static const MemoryRegionOps max78000_gcr_ops = {
    .read = max78000_gcr_read,
    .write = max78000_gcr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void max78000_gcr_init(Object *obj)
{
    Max78000GcrState *s = MAX78000_GCR(obj);

    memory_region_init_io(&s->mmio, obj, &max78000_gcr_ops, s,
                          TYPE_MAX78000_GCR, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

}

static void max78000_gcr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, max78000_gcr_reset);
}

static const TypeInfo max78000_gcr_info = {
    .name          = TYPE_MAX78000_GCR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000GcrState),
    .instance_init = max78000_gcr_init,
    .class_init     = max78000_gcr_class_init,
};

static void max78000_gcr_register_types(void)
{
    type_register_static(&max78000_gcr_info);
}

type_init(max78000_gcr_register_types)
