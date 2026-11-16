/*
 * Emulatorul cititorului IBV: ADC-ul SAR din blocul SENS al ESP32-S3.
 *
 * Doar citirea „oneshot” pe care o folosește ESP-IDF: firmware-ul alege
 * canalul, pornește măsurarea și așteaptă bitul de terminat. Fără acest bloc
 * firmware-ul rămâne agățat la pornire, în autocalibrarea ADC-ului.
 *
 * Scala: 4095 înseamnă 3100 mV pe pin (atenuarea de 12 dB), adică exact
 * formula pe care o aplică firmware-ul când cipul n-are calibrare în eFuse.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "ibv.h"

#define SENS_SIZE 0x400
#define A_MEAS1_CTRL2 0x0C
#define A_MEAS2_CTRL2 0x30
#define A_TSENS_CTRL 0x50

#define MEAS_DATA_MASK 0xffffu
#define MEAS_DONE (1u << 16)
#define MEAS_START (1u << 17)
#define MEAS_PAD(v) (((v) >> 19) & 0xfff)
#define TSENS_READY (1u << 8)

#define ADC_CHANNELS 10
#define FULL_SCALE_MV 3100

typedef struct IbvSensState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[SENS_SIZE / 4];
    int mv[ADC_CHANNELS];
} IbvSensState;

OBJECT_DECLARE_SIMPLE_TYPE(IbvSensState, IBV_S3_SENS)

static uint32_t convert(IbvSensState *s, uint32_t ctrl)
{
    uint32_t pads = MEAS_PAD(ctrl);
    int ch = pads ? ctz32(pads) : -1;
    int64_t raw;

    if (ch < 0 || ch >= ADC_CHANNELS) {
        return 0;    /* niciun pin: masa internă, cum cere autocalibrarea */
    }
    /* Rotunjit în sus, ca împărțirea întreagă din firmware să dea înapoi mV-ul pus aici. */
    raw = ((int64_t)s->mv[ch] * 4095 + FULL_SCALE_MV - 1) / FULL_SCALE_MV;
    return MIN(MAX(raw, 0), 4095);
}

static uint64_t sens_read(void *opaque, hwaddr addr, unsigned size)
{
    IbvSensState *s = opaque;

    if (addr == A_TSENS_CTRL) {
        return s->regs[addr / 4] | TSENS_READY;
    }
    return s->regs[addr / 4];
}

static void sens_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IbvSensState *s = opaque;
    uint32_t v = value;

    if (addr == A_MEAS1_CTRL2 || addr == A_MEAS2_CTRL2) {
        v &= ~(MEAS_DONE | MEAS_DATA_MASK);
        if (v & MEAS_START) {
            /* ADC2 nu e legat la nimic pe placă. */
            v |= MEAS_DONE | (addr == A_MEAS1_CTRL2 ? convert(s, v) : 0);
        }
    }
    s->regs[addr / 4] = v;
}

static const MemoryRegionOps sens_ops = {
    .read = sens_read,
    .write = sens_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

void ibv_sens_set_mv(DeviceState *dev, int channel, int mv)
{
    IBV_S3_SENS(dev)->mv[channel] = mv;
}

static void sens_reset_hold(Object *obj, ResetType type)
{
    IbvSensState *s = IBV_S3_SENS(obj);

    memset(s->regs, 0, sizeof s->regs);
}

static void sens_init(Object *obj)
{
    IbvSensState *s = IBV_S3_SENS(obj);

    memory_region_init_io(&s->iomem, obj, &sens_ops, s, TYPE_IBV_S3_SENS, SENS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void sens_class_init(ObjectClass *klass, void *data)
{
    RESETTABLE_CLASS(klass)->phases.hold = sens_reset_hold;
}

static const TypeInfo sens_info = {
    .name = TYPE_IBV_S3_SENS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbvSensState),
    .instance_init = sens_init,
    .class_init = sens_class_init,
};

static void sens_register_types(void)
{
    type_register_static(&sens_info);
}

type_init(sens_register_types)
