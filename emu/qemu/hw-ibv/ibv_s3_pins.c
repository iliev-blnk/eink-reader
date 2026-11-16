/*
 * Emulatorul cititorului IBV: pinii ESP32-S3 (GPIO, IO_MUX, RTC IO).
 *
 * Modelul din QEMU leagă doar ieșirile înapoi la intrări. Aici fiecare pin
 * are un nivel electric adevărat: îl ține ieșirea cipului, un dispozitiv din
 * afară, o rezistență internă sau nimeni. Un pin lăsat în aer se citește la
 * nivelul dat de placă (implicit 0), deci o rezistență de pull-up uitată în
 * firmware se vede aici la fel ca pe masă.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "ibv.h"

#define GPIO_SIZE 0x1000
#define IOMUX_SIZE 0x1000
#define RTCIO_SIZE 0x400

/* GPIO */
#define A_OUT 0x04
#define A_OUT1 0x10
#define A_ENABLE 0x20
#define A_ENABLE1 0x2C
#define A_STRAP 0x38
#define A_IN 0x3C
#define A_IN1 0x40
#define A_STATUS 0x44
#define A_STATUS1 0x50
#define A_PCPU_INT 0x5C
#define A_PCPU_NMI_INT 0x60
#define A_CPUSDIO_INT 0x64
#define A_PCPU_INT1 0x68
#define A_PCPU_NMI_INT1 0x6C
#define A_CPUSDIO_INT1 0x70
#define A_PIN0 0x74
#define A_STATUS_NEXT 0x14C
#define A_STATUS_NEXT1 0x150
#define A_FUNC_OUT_SEL0 0x554

#define PIN_PAD_DRIVER (1u << 2)
#define PIN_INT_TYPE(v) (((v) >> 7) & 7)
#define PIN_INT_ENA_PRO (1u << 13)
#define PIN_INT_ENA_PRO_NMI (1u << 14)

#define OUT_SEL_GPIO 256
#define OUT_SEL_FSPICS0 110
#define OUT_SEL_INV (1u << 9)
#define FSPICS0_PIN 10      /* pinul pe care CS0 iese direct prin IO_MUX */
#define FSPI_IOMUX_FUNC 4

/* IO_MUX */
#define MUX_FUN_PD (1u << 7)
#define MUX_FUN_PU (1u << 8)
#define MUX_FUN_IE (1u << 9)
#define MUX_MCU_SEL(v) (((v) >> 12) & 7)
#define MUX_RESET 0x0A00    /* intrarea activă, curentul 2 */

/* RTC IO */
#define A_RTC_OUT 0x00
#define A_RTC_OUT_W1TS 0x04
#define A_RTC_OUT_W1TC 0x08
#define A_RTC_ENABLE 0x0C
#define A_RTC_ENABLE_W1TS 0x10
#define A_RTC_ENABLE_W1TC 0x14
#define A_RTC_IN 0x24
#define A_RTC_PAD0 0x84
#define RTC_SHIFT 10
#define RTC_PAD_FUN_IE (1u << 13)
#define RTC_PAD_MUX_SEL (1u << 19)
#define RTC_PAD_RUE (1u << 27)
#define RTC_PAD_RDE (1u << 28)

typedef struct IbvPinsState {
    SysBusDevice parent_obj;
    MemoryRegion gpio_mr, iomux_mr, rtcio_mr;
    qemu_irq irq;
    qemu_irq pad[IBV_PIN_COUNT];

    /* domeniul digital */
    uint32_t gpio[GPIO_SIZE / 4];
    uint32_t iomux[IOMUX_SIZE / 4];
    uint64_t out, enable, status;
    /* domeniul RTC: trece peste resetul digital */
    uint32_t rtcio[RTCIO_SIZE / 4];
    uint32_t rtc_out, rtc_enable;
    uint32_t hold;
    int8_t held_drive[IBV_RTC_PIN_COUNT], held_pull[IBV_RTC_PIN_COUNT];

    /* lumea din afară */
    int8_t ext[IBV_PIN_COUNT];
    int8_t float_level[IBV_PIN_COUNT];
    bool sig_cs[IBV_GPSPI_CS_COUNT];

    int8_t level[IBV_PIN_COUNT];   /* -1 = încă neanunțat */
    uint64_t in;                   /* ce vede registrul IN */
    uint32_t strap_mode;

    void (*listener)(void *opaque);
    void *listener_opaque;
} IbvPinsState;

OBJECT_DECLARE_SIMPLE_TYPE(IbvPinsState, IBV_S3_PINS)

static uint32_t mux_reg(IbvPinsState *s, int pin)
{
    return s->iomux[1 + pin];
}

static uint32_t rtc_pad(IbvPinsState *s, int pin)
{
    return s->rtcio[A_RTC_PAD0 / 4 + pin];
}

static bool rtc_mode(IbvPinsState *s, int pin)
{
    return pin < IBV_RTC_PIN_COUNT && (rtc_pad(s, pin) & RTC_PAD_MUX_SEL);
}

/* Ce pune cipul pe pin: -1 nimic, 0 sau 1. */
static int cfg_drive(IbvPinsState *s, int pin)
{
    if (rtc_mode(s, pin)) {
        if (!(s->rtc_enable >> pin & 1)) {
            return -1;
        }
        return s->rtc_out >> pin & 1;
    }
    uint32_t fn = MUX_MCU_SEL(mux_reg(s, pin));
    uint32_t cfg = s->gpio[(A_FUNC_OUT_SEL0 + 4 * pin) / 4], sel = cfg & 0x1ff;
    if (fn == FSPI_IOMUX_FUNC && pin == FSPICS0_PIN) {
        return s->sig_cs[0];
    }
    if (fn <= 1 && sel >= OUT_SEL_FSPICS0 && sel < OUT_SEL_FSPICS0 + IBV_GPSPI_CS_COUNT) {
        return s->sig_cs[sel - OUT_SEL_FSPICS0] ^ !!(cfg & OUT_SEL_INV);
    }
    if (fn > 1 || sel != OUT_SEL_GPIO) {
        /* Pinul e al altui semnal de periferic (ceasul, datele SPI): în repaus sus. */
        return 1;
    }
    if (!(s->enable >> pin & 1)) {
        return -1;
    }
    int v = s->out >> pin & 1;
    if (v && (s->gpio[(A_PIN0 + 4 * pin) / 4] & PIN_PAD_DRIVER)) {
        return -1;    /* drenă în gol: 1 înseamnă lăsat liber */
    }
    return v;
}

static int cfg_pull(IbvPinsState *s, int pin)
{
    bool up, down;

    if (rtc_mode(s, pin)) {
        up = rtc_pad(s, pin) & RTC_PAD_RUE;
        down = rtc_pad(s, pin) & RTC_PAD_RDE;
    } else {
        up = mux_reg(s, pin) & MUX_FUN_PU;
        down = mux_reg(s, pin) & MUX_FUN_PD;
    }
    return up ? 1 : down ? 0 : -1;
}

static bool held(IbvPinsState *s, int pin)
{
    return pin < IBV_RTC_PIN_COUNT && (s->hold >> pin & 1);
}

static int pad_level(IbvPinsState *s, int pin)
{
    int drive = held(s, pin) ? s->held_drive[pin] : cfg_drive(s, pin);
    int pull = held(s, pin) ? s->held_pull[pin] : cfg_pull(s, pin);

    if (drive >= 0) {
        return drive;
    }
    if (s->ext[pin] >= 0) {
        return s->ext[pin];
    }
    if (pull >= 0) {
        return pull;
    }
    return s->float_level[pin];
}

static uint64_t int_enabled(IbvPinsState *s, uint32_t bit)
{
    uint64_t m = 0;

    for (int pin = 0; pin < IBV_PIN_COUNT; pin++) {
        if (s->gpio[(A_PIN0 + 4 * pin) / 4] & bit) {
            m |= 1ull << pin;
        }
    }
    return m;
}

/* Recalculează nivelurile după orice schimbare și anunță pe toată lumea. */
static void pins_update(IbvPinsState *s)
{
    bool changed = false;
    uint64_t in = 0;

    for (int pin = 0; pin < IBV_PIN_COUNT; pin++) {
        int lv = pad_level(s, pin);
        bool ie = mux_reg(s, pin) & MUX_FUN_IE;
        bool now = ie && lv, before = s->in >> pin & 1;
        int type = PIN_INT_TYPE(s->gpio[(A_PIN0 + 4 * pin) / 4]);

        if (now) {
            in |= 1ull << pin;
        }
        if ((type == 1 && !before && now) || (type == 2 && before && !now) ||
            (type == 3 && before != now) || (type == 4 && !now) || (type == 5 && now)) {
            s->status |= 1ull << pin;
        }
        if (lv != s->level[pin]) {
            s->level[pin] = lv;
            qemu_set_irq(s->pad[pin], lv);
            changed = true;
        }
    }
    s->in = in;
    qemu_set_irq(s->irq, (s->status & int_enabled(s, PIN_INT_ENA_PRO)) != 0);
    if (changed && s->listener) {
        s->listener(s->listener_opaque);
    }
}

/* ---- GPIO ---- */

static uint64_t *pair_reg(IbvPinsState *s, hwaddr addr, int *hi, int *op)
{
    static const struct {
        hwaddr base;
        int hi;
    } groups[] = {{A_OUT, 0}, {A_OUT1, 1}, {A_ENABLE, 0}, {A_ENABLE1, 1}, {A_STATUS, 0}, {A_STATUS1, 1}};

    for (int i = 0; i < ARRAY_SIZE(groups); i++) {
        if (addr >= groups[i].base && addr < groups[i].base + 12) {
            *hi = groups[i].hi;
            *op = (addr - groups[i].base) / 4;
            return i < 2 ? &s->out : i < 4 ? &s->enable : &s->status;
        }
    }
    return NULL;
}

static uint64_t gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    IbvPinsState *s = opaque;
    int hi, op;
    uint64_t *reg = pair_reg(s, addr, &hi, &op);

    if (reg) {
        return op == 0 ? (uint32_t)(*reg >> (hi ? 32 : 0)) : 0;
    }
    switch (addr) {
    case A_STRAP:
        return s->strap_mode;
    case A_IN:
        return (uint32_t)s->in;
    case A_IN1:
        return (uint32_t)(s->in >> 32);
    case A_PCPU_INT:
        return (uint32_t)(s->status & int_enabled(s, PIN_INT_ENA_PRO));
    case A_PCPU_INT1:
        return (uint32_t)((s->status & int_enabled(s, PIN_INT_ENA_PRO)) >> 32);
    case A_PCPU_NMI_INT:
        return (uint32_t)(s->status & int_enabled(s, PIN_INT_ENA_PRO_NMI));
    case A_PCPU_NMI_INT1:
        return (uint32_t)((s->status & int_enabled(s, PIN_INT_ENA_PRO_NMI)) >> 32);
    case A_CPUSDIO_INT:
    case A_CPUSDIO_INT1:
        return 0;
    case A_STATUS_NEXT:
        return (uint32_t)s->status;
    case A_STATUS_NEXT1:
        return (uint32_t)(s->status >> 32);
    }
    return s->gpio[addr / 4];
}

static void gpio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IbvPinsState *s = opaque;
    int hi, op;
    uint64_t *reg = pair_reg(s, addr, &hi, &op);

    if (reg) {
        uint64_t v = (uint64_t)(uint32_t)value << (hi ? 32 : 0);
        uint64_t mask = 0xffffffffull << (hi ? 32 : 0);

        if (op == 0) {
            *reg = (*reg & ~mask) | v;
        } else if (op == 1) {
            *reg |= v;
        } else {
            *reg &= ~v;
        }
        *reg &= (1ull << IBV_PIN_COUNT) - 1;
    } else if (addr != A_STRAP && addr != A_IN && addr != A_IN1) {
        s->gpio[addr / 4] = value;
    }
    pins_update(s);
}

static const MemoryRegionOps gpio_ops = {
    .read = gpio_read,
    .write = gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* ---- IO_MUX ---- */

static uint64_t iomux_read(void *opaque, hwaddr addr, unsigned size)
{
    IbvPinsState *s = opaque;

    return s->iomux[addr / 4];
}

static void iomux_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IbvPinsState *s = opaque;

    s->iomux[addr / 4] = value;
    pins_update(s);
}

static const MemoryRegionOps iomux_ops = {
    .read = iomux_read,
    .write = iomux_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* ---- RTC IO ---- */

static uint64_t rtcio_read(void *opaque, hwaddr addr, unsigned size)
{
    IbvPinsState *s = opaque;
    uint32_t in = 0;

    switch (addr) {
    case A_RTC_OUT:
        return s->rtc_out << RTC_SHIFT;
    case A_RTC_ENABLE:
        return s->rtc_enable << RTC_SHIFT;
    case A_RTC_IN:
        for (int pin = 0; pin < IBV_RTC_PIN_COUNT; pin++) {
            if ((rtc_pad(s, pin) & RTC_PAD_FUN_IE) && pad_level(s, pin)) {
                in |= 1u << pin;
            }
        }
        return in << RTC_SHIFT;
    case A_RTC_OUT_W1TS:
    case A_RTC_OUT_W1TC:
    case A_RTC_ENABLE_W1TS:
    case A_RTC_ENABLE_W1TC:
        return 0;
    }
    return s->rtcio[addr / 4];
}

static void rtcio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IbvPinsState *s = opaque;
    uint32_t bits = (uint32_t)value >> RTC_SHIFT;

    switch (addr) {
    case A_RTC_OUT:
        s->rtc_out = bits;
        break;
    case A_RTC_OUT_W1TS:
        s->rtc_out |= bits;
        break;
    case A_RTC_OUT_W1TC:
        s->rtc_out &= ~bits;
        break;
    case A_RTC_ENABLE:
        s->rtc_enable = bits;
        break;
    case A_RTC_ENABLE_W1TS:
        s->rtc_enable |= bits;
        break;
    case A_RTC_ENABLE_W1TC:
        s->rtc_enable &= ~bits;
        break;
    case A_RTC_IN:
        break;
    default:
        s->rtcio[addr / 4] = value;
    }
    pins_update(s);
}

static const MemoryRegionOps rtcio_ops = {
    .read = rtcio_read,
    .write = rtcio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* ---- pentru placă ---- */

void ibv_pins_set_ext(DeviceState *dev, int pin, int level)
{
    IbvPinsState *s = IBV_S3_PINS(dev);

    s->ext[pin] = level;
    pins_update(s);
}

void ibv_pins_set_float_level(DeviceState *dev, int pin, int level)
{
    IbvPinsState *s = IBV_S3_PINS(dev);

    s->float_level[pin] = level;
    pins_update(s);
}

int ibv_pins_level(DeviceState *dev, int pin)
{
    return pad_level(IBV_S3_PINS(dev), pin);
}

uint32_t ibv_pins_route(DeviceState *dev, int pin)
{
    IbvPinsState *s = IBV_S3_PINS(dev);
    uint32_t fn = MUX_MCU_SEL(mux_reg(s, pin));

    if (fn > 1) {
        return 0x1000 | fn;
    }
    return s->gpio[(A_FUNC_OUT_SEL0 + 4 * pin) / 4] & 0x1ff;
}

void ibv_pins_set_hold(DeviceState *dev, uint32_t mask)
{
    IbvPinsState *s = IBV_S3_PINS(dev);

    for (int pin = 0; pin < IBV_RTC_PIN_COUNT; pin++) {
        if ((mask >> pin & 1) && !(s->hold >> pin & 1)) {
            s->held_drive[pin] = cfg_drive(s, pin);
            s->held_pull[pin] = cfg_pull(s, pin);
        }
    }
    s->hold = mask;
    pins_update(s);
}

static void digital_reset(IbvPinsState *s)
{
    memset(s->gpio, 0, sizeof s->gpio);
    memset(s->iomux, 0, sizeof s->iomux);
    for (int pin = 0; pin < IBV_PIN_COUNT; pin++) {
        s->gpio[(A_FUNC_OUT_SEL0 + 4 * pin) / 4] = OUT_SEL_GPIO;
        s->iomux[1 + pin] = MUX_RESET;
    }
    s->iomux[1 + 0] |= MUX_FUN_PU;    /* GPIO0, pinul de boot */
    s->out = s->enable = s->status = 0;
}

void ibv_pins_digital_off(DeviceState *dev)
{
    IbvPinsState *s = IBV_S3_PINS(dev);

    digital_reset(s);
    pins_update(s);
}

void ibv_pins_rtc_off(DeviceState *dev)
{
    IbvPinsState *s = IBV_S3_PINS(dev);

    memset(s->rtcio, 0, sizeof s->rtcio);
    s->rtc_out = s->rtc_enable = 0;
    pins_update(s);
}

void ibv_pins_set_listener(DeviceState *dev, void (*fn)(void *opaque), void *opaque)
{
    IbvPinsState *s = IBV_S3_PINS(dev);

    s->listener = fn;
    s->listener_opaque = opaque;
}

static void ext_in(void *opaque, int pin, int level)
{
    IbvPinsState *s = opaque;

    s->ext[pin] = !!level;
    pins_update(s);
}

static void sig_cs_in(void *opaque, int n, int level)
{
    IbvPinsState *s = opaque;

    s->sig_cs[n] = level;
    pins_update(s);
}

static void pins_reset_hold(Object *obj, ResetType type)
{
    IbvPinsState *s = IBV_S3_PINS(obj);

    digital_reset(s);
    /* Toate liniile se anunță din nou, ca dispozitivele să pornească de la nivelul bun. */
    memset(s->level, -1, sizeof s->level);
    pins_update(s);
}

static void pins_init(Object *obj)
{
    IbvPinsState *s = IBV_S3_PINS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->gpio_mr, obj, &gpio_ops, s, "ibv.s3.gpio", GPIO_SIZE);
    memory_region_init_io(&s->iomux_mr, obj, &iomux_ops, s, "ibv.s3.iomux", IOMUX_SIZE);
    memory_region_init_io(&s->rtcio_mr, obj, &rtcio_ops, s, "ibv.s3.rtcio", RTCIO_SIZE);
    sysbus_init_mmio(sbd, &s->gpio_mr);
    sysbus_init_mmio(sbd, &s->iomux_mr);
    sysbus_init_mmio(sbd, &s->rtcio_mr);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), s->pad, IBV_PINS_PAD, IBV_PIN_COUNT);
    qdev_init_gpio_in_named(DEVICE(obj), ext_in, IBV_PINS_EXT, IBV_PIN_COUNT);
    qdev_init_gpio_in_named(DEVICE(obj), sig_cs_in, IBV_PINS_SIG_CS, IBV_GPSPI_CS_COUNT);
    memset(s->sig_cs, 1, sizeof s->sig_cs);
    memset(s->ext, -1, sizeof s->ext);
    memset(s->level, -1, sizeof s->level);
    digital_reset(s);
}

static Property pins_properties[] = {
    DEFINE_PROP_UINT32("strap_mode", IbvPinsState, strap_mode, 0x4),
    DEFINE_PROP_END_OF_LIST(),
};

static void pins_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = pins_reset_hold;
    device_class_set_props(dc, pins_properties);
}

static const TypeInfo pins_info = {
    .name = TYPE_IBV_S3_PINS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbvPinsState),
    .instance_init = pins_init,
    .class_init = pins_class_init,
};

static void pins_register_types(void)
{
    type_register_static(&pins_info);
}

type_init(pins_register_types)
