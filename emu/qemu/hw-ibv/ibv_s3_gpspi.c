/*
 * Emulatorul cititorului IBV: SPI2 (GPSPI) al ESP32-S3, ca master.
 *
 * Cât îi trebuie driverului spi_master din ESP-IDF: faze de comandă, adresă
 * și date, date prin registrele W0..W15 sau prin GDMA, șase linii CS și
 * întreruperea de tranzacție terminată. Tranzacția durează cât ar dura pe fir
 * la ceasul programat, deci timpii măsurați de firmware au sens.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/dma/esp_gdma.h"
#include "ibv.h"

#define GPSPI_SIZE 0x100
#define A_CMD 0x00
#define A_ADDR 0x04
#define A_CLOCK 0x0C
#define A_USER 0x10
#define A_USER1 0x14
#define A_USER2 0x18
#define A_MS_DLEN 0x1C
#define A_MISC 0x20
#define A_DMA_CONF 0x30
#define A_INT_ENA 0x34
#define A_INT_CLR 0x38
#define A_INT_RAW 0x3C
#define A_INT_ST 0x40
#define A_INT_SET 0x44
#define A_W0 0x98
#define A_DATE 0xF0

#define CMD_USR (1u << 24)
#define CMD_UPDATE (1u << 23)
#define USER_COMMAND (1u << 31)
#define USER_ADDR (1u << 30)
#define USER_DUMMY (1u << 29)
#define USER_MISO (1u << 28)
#define USER_MOSI (1u << 27)
#define USER_MOSI_HIGHPART (1u << 25)
#define USER_MISO_HIGHPART (1u << 24)
#define USER_DOUTDIN (1u << 0)
#define MISC_CS_KEEP_ACTIVE (1u << 30)
#define DMA_RX_ENA (1u << 27)
#define DMA_TX_ENA (1u << 28)
#define INT_TRANS_DONE (1u << 12)
#define CLOCK_EQU_SYSCLK (1u << 31)

#define APB_HZ 80000000
#define W_BYTES 64

typedef struct IbvGpspiState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    SSIBus *bus;
    qemu_irq irq;
    qemu_irq cs[IBV_GPSPI_CS_COUNT];
    ESPGdmaState *gdma;
    QEMUTimer *done;
    uint32_t regs[GPSPI_SIZE / 4];
    bool miso_driven;
    uint64_t xfers;
    void (*start_hook)(void *opaque);
    void *start_opaque;
} IbvGpspiState;

OBJECT_DECLARE_SIMPLE_TYPE(IbvGpspiState, IBV_S3_GPSPI)

static void update_irq(IbvGpspiState *s)
{
    qemu_set_irq(s->irq, (s->regs[A_INT_RAW / 4] & s->regs[A_INT_ENA / 4]) != 0);
}

static void set_cs(IbvGpspiState *s, bool active)
{
    uint32_t misc = s->regs[A_MISC / 4];

    for (int i = 0; i < IBV_GPSPI_CS_COUNT; i++) {
        bool on = active && !(misc >> i & 1);
        bool active_high = misc >> (7 + i) & 1;

        qemu_set_irq(s->cs[i], on == active_high);
    }
}

static uint8_t xfer(IbvGpspiState *s, uint8_t tx)
{
    uint8_t rx = ssi_transfer(s->bus, tx);

    return s->miso_driven ? rx : 0xff;
}

static uint8_t w_get(IbvGpspiState *s, uint32_t i)
{
    i %= W_BYTES;
    return s->regs[A_W0 / 4 + i / 4] >> (8 * (i % 4));
}

static void w_put(IbvGpspiState *s, uint32_t i, uint8_t v)
{
    uint32_t *w = &s->regs[A_W0 / 4 + (i % W_BYTES) / 4];
    int sh = 8 * (i % 4);

    *w = (*w & ~(0xffu << sh)) | ((uint32_t)v << sh);
}

static int64_t bit_ns(IbvGpspiState *s)
{
    uint32_t clk = s->regs[A_CLOCK / 4];
    uint32_t div = 1;

    if (!(clk & CLOCK_EQU_SYSCLK)) {
        div = (((clk >> 18) & 0xf) + 1) * (((clk >> 12) & 0x3f) + 1);
    }
    return (int64_t)NANOSECONDS_PER_SECOND * div / APB_HZ;
}

static void transaction(IbvGpspiState *s)
{
    uint32_t user = s->regs[A_USER / 4], user1 = s->regs[A_USER1 / 4], user2 = s->regs[A_USER2 / 4];
    uint32_t dma = s->regs[A_DMA_CONF / 4];
    uint32_t bits = 0;

    s->xfers++;
    if (s->start_hook) {
        s->start_hook(s->start_opaque);
    }
    set_cs(s, true);

    if (user & USER_COMMAND) {
        uint32_t n = (user2 >> 28) + 1;

        for (uint32_t i = 0; i < (n + 7) / 8; i++) {
            xfer(s, user2 >> (8 * i));
        }
        bits += n;
    }
    if (user & USER_ADDR) {
        uint32_t n = (user1 >> 27) + 1, addr = s->regs[A_ADDR / 4];

        for (uint32_t i = 0; i < (n + 7) / 8; i++) {
            xfer(s, addr >> (24 - 8 * i));
        }
        bits += n;
    }
    if (user & USER_DUMMY) {
        uint32_t n = (user1 & 0xff) + 1;

        for (uint32_t i = 0; i < (n + 7) / 8; i++) {
            xfer(s, 0xff);
        }
        bits += n;
    }
    if (user & (USER_MOSI | USER_MISO)) {
        uint32_t n = (s->regs[A_MS_DLEN / 4] & 0x3ffff) + 1, len = (n + 7) / 8;
        g_autofree uint8_t *tx = g_malloc(len), *rx = g_malloc(len);
        bool mosi = user & USER_MOSI, miso = user & USER_MISO;
        uint32_t chan;

        memset(tx, 0xff, len);
        if (mosi && (dma & DMA_TX_ENA)) {
            if (!s->gdma || !esp_gdma_get_channel_periph(s->gdma, GDMA_SPI2, ESP_GDMA_OUT_IDX, &chan) ||
                !esp_gdma_read_channel(s->gdma, chan, tx, len)) {
                ibv_warn("SPI2: transmisie prin DMA de %u octeți fără un canal GDMA pregătit", len);
            }
        } else if (mosi) {
            for (uint32_t i = 0; i < len; i++) {
                tx[i] = w_get(s, i + (user & USER_MOSI_HIGHPART ? 32 : 0));
            }
        }
        if (mosi && miso && !(user & USER_DOUTDIN)) {
            /* Semiduplex: întâi se scrie, apoi se citește. */
            for (uint32_t i = 0; i < len; i++) {
                xfer(s, tx[i]);
            }
            memset(tx, 0xff, len);
            bits += n;
        }
        for (uint32_t i = 0; i < len; i++) {
            rx[i] = xfer(s, tx[i]);
        }
        bits += n;
        if (miso && (dma & DMA_RX_ENA)) {
            if (!s->gdma || !esp_gdma_get_channel_periph(s->gdma, GDMA_SPI2, ESP_GDMA_IN_IDX, &chan) ||
                !esp_gdma_write_channel(s->gdma, chan, rx, len)) {
                ibv_warn("SPI2: recepție prin DMA de %u octeți fără un canal GDMA pregătit", len);
            }
        } else if (miso) {
            for (uint32_t i = 0; i < len; i++) {
                w_put(s, i + (user & USER_MISO_HIGHPART ? 32 : 0), rx[i]);
            }
        }
    }

    if (!(s->regs[A_MISC / 4] & MISC_CS_KEEP_ACTIVE)) {
        set_cs(s, false);
    }
    timer_mod(s->done, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + MAX(bits, 1) * bit_ns(s));
}

static void transaction_done(void *opaque)
{
    IbvGpspiState *s = opaque;

    s->regs[A_CMD / 4] &= ~CMD_USR;
    s->regs[A_INT_RAW / 4] |= INT_TRANS_DONE;
    update_irq(s);
}

static uint64_t gpspi_read(void *opaque, hwaddr addr, unsigned size)
{
    IbvGpspiState *s = opaque;

    switch (addr) {
    case A_INT_ST:
        return s->regs[A_INT_RAW / 4] & s->regs[A_INT_ENA / 4];
    case A_INT_CLR:
    case A_INT_SET:
        return 0;
    case A_DATE:
        return 0x2101190;
    }
    return s->regs[addr / 4];
}

static void gpspi_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IbvGpspiState *s = opaque;
    uint32_t v = value;

    switch (addr) {
    case A_CMD:
        /* UPDATE mută configurația în domeniul de ceas al SPI; aici e pe loc. */
        v &= ~CMD_UPDATE;
        if (s->regs[A_CMD / 4] & CMD_USR) {
            if (v & CMD_USR) {
                ibv_warn("SPI2: tranzacție pornită cât cea dinainte încă merge");
            }
            return;
        }
        s->regs[A_CMD / 4] = v;
        if (v & CMD_USR) {
            transaction(s);
        }
        return;
    case A_INT_CLR:
        s->regs[A_INT_RAW / 4] &= ~v;
        break;
    case A_INT_SET:
        s->regs[A_INT_RAW / 4] |= v;
        break;
    case A_INT_ST:
        break;
    default:
        s->regs[addr / 4] = v;
    }
    update_irq(s);
}

static const MemoryRegionOps gpspi_ops = {
    .read = gpspi_read,
    .write = gpspi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

void ibv_gpspi_set_miso_driven(DeviceState *dev, bool driven)
{
    IBV_S3_GPSPI(dev)->miso_driven = driven;
}

uint64_t ibv_gpspi_xfer_count(DeviceState *dev)
{
    return IBV_S3_GPSPI(dev)->xfers;
}

void ibv_gpspi_set_start_hook(DeviceState *dev, void (*fn)(void *opaque), void *opaque)
{
    IbvGpspiState *s = IBV_S3_GPSPI(dev);

    s->start_hook = fn;
    s->start_opaque = opaque;
}

static void gpspi_reset_hold(Object *obj, ResetType type)
{
    IbvGpspiState *s = IBV_S3_GPSPI(obj);

    timer_del(s->done);
    memset(s->regs, 0, sizeof s->regs);
    s->regs[A_USER / 4] = 0x800000c0;
    s->regs[A_MISC / 4] = 0x3e;
    set_cs(s, false);
    update_irq(s);
}

static void gpspi_realize(DeviceState *dev, Error **errp)
{
    IbvGpspiState *s = IBV_S3_GPSPI(dev);

    s->done = timer_new_ns(QEMU_CLOCK_VIRTUAL, transaction_done, s);
}

static void gpspi_init(Object *obj)
{
    IbvGpspiState *s = IBV_S3_GPSPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &gpspi_ops, s, TYPE_IBV_S3_GPSPI, GPSPI_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), s->cs, SSI_GPIO_CS, IBV_GPSPI_CS_COUNT);
    s->bus = ssi_create_bus(DEVICE(obj), "spi");
    object_property_add_link(obj, "gdma", TYPE_ESP_GDMA, (Object **)&s->gdma,
                             qdev_prop_allow_set_link_before_realize, OBJ_PROP_LINK_STRONG);
}

static void gpspi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    RESETTABLE_CLASS(klass)->phases.hold = gpspi_reset_hold;
    dc->realize = gpspi_realize;
}

static const TypeInfo gpspi_info = {
    .name = TYPE_IBV_S3_GPSPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbvGpspiState),
    .instance_init = gpspi_init,
    .class_init = gpspi_class_init,
};

static void gpspi_register_types(void)
{
    type_register_static(&gpspi_info);
}

type_init(gpspi_register_types)
