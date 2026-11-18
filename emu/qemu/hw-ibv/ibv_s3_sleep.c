/*
 * Emulatorul cititorului IBV: somnul ESP32-S3, peste RTC_CNTL din QEMU.
 *
 * Modelul RTC_CNTL din QEMU știe resetul, ceasurile și registrele STORE, dar
 * nu doarme. Dispozitivul ăsta se așază peste el: prinde registrele somnului
 * și ale trezirii, iar restul le dă mai departe neschimbate.
 *
 * La SLEEP_EN procesoarele se opresc. Trezirea vine de la EXT1 (pinii RTC
 * aleși, la nivelul ales), de la temporizator sau, doar din somnul ușor, de
 * la un GPIO cu trezirea pornită, la nivelul cerut de tipul întreruperii. Din somnul adânc cipul se
 * trezește prin reset, cu cauza „somn adânc” și cu registrele RTC păstrate.
 * În somnul ușor stă și temporizatorul sistemului (cristalul e oprit), ca
 * ESP-IDF să nu numere timpul somnului de două ori.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/misc/esp32s3_rtc_cntl.h"
#include "hw/timer/esp_systimer.h"
#include "ibv.h"

#define A_SLP_TIMER0 0x04
#define A_SLP_TIMER1 0x08
#define A_TIME_UPDATE 0x0C
#define A_TIME_LOW0 0x10
#define A_TIME_HIGH0 0x14
#define A_STATE0 0x18
#define A_WAKEUP_STATE 0x3C
#define A_INT_ENA 0x40
#define A_INT_RAW 0x44
#define A_INT_ST 0x48
#define A_INT_CLR 0x4C
#define A_EXT_WAKEUP_CONF 0x64
#define A_SLP_REJECT_CONF 0x68
#define A_PWC 0x88
#define A_DIG_PWC 0x90
#define A_DIG_ISO 0x94
#define A_PAD_HOLD 0xD8
#define A_DIG_PAD_HOLD 0xDC
#define A_EXT_WAKEUP1 0xE0
#define A_EXT_WAKEUP1_STATUS 0xE4
#define A_SLP_REJECT_CAUSE 0x128
#define A_SLP_WAKEUP_CAUSE 0x130
#define A_INT_ENA_W1TS 0x138
#define A_INT_ENA_W1TC 0x13C

#define STATE0_SLEEP_EN (1u << 31)
#define SLP_TIMER1_ALARM_EN (1u << 16)
#define WAKEUP_ENA_SHIFT 15
#define INT_SLP_WAKEUP (1u << 0)
#define EXT_WAKEUP1_LV (1u << 31)
#define EXT_WAKEUP1_SEL 0x3fffffu
#define EXT_WAKEUP1_STATUS_CLR (1u << 22)
#define PWC_PD_EN (1u << 20)
#define DIG_PWC_DG_WRAP_PD_EN (1u << 31)

#define TRIG_EXT1 (1u << 1)
#define TRIG_GPIO (1u << 2)    /* doar din somnul ușor */
#define TRIG_TIMER (1u << 3)

#define FALL_ASLEEP_NS 200000    /* cât îi ia cipului să adoarmă de-a binelea */

typedef struct IbvSleepState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    Esp32s3RtcCntlState *rtc;
    ESPSysTimerState *systimer;
    DeviceState *pins;
    QEMUTimer *settle, *alarm;
    uint32_t regs[ESP32S3_RTC_CNTL_SIZE / 4];
    bool asleep, settled, deep;
    bool wake_reset;    /* resetul de trezire din somnul adânc e cerut, dar încă nu s-a făcut */
    int64_t slept_at;
} IbvSleepState;

OBJECT_DECLARE_SIMPLE_TYPE(IbvSleepState, IBV_S3_SLEEP)

static bool intercepted(hwaddr addr)
{
    switch (addr) {
    case A_SLP_TIMER0:
    case A_SLP_TIMER1:
    case A_STATE0:
    case A_WAKEUP_STATE:
    case A_INT_ENA:
    case A_INT_RAW:
    case A_INT_ST:
    case A_INT_CLR:
    case A_EXT_WAKEUP_CONF:
    case A_SLP_REJECT_CONF:
    case A_PWC:
    case A_DIG_PWC:
    case A_DIG_ISO:
    case A_PAD_HOLD:
    case A_DIG_PAD_HOLD:
    case A_EXT_WAKEUP1:
    case A_EXT_WAKEUP1_STATUS:
    case A_SLP_REJECT_CAUSE:
    case A_SLP_WAKEUP_CAUSE:
    case A_INT_ENA_W1TS:
    case A_INT_ENA_W1TC:
        return true;
    }
    return false;
}

static uint64_t rtc_read(IbvSleepState *s, hwaddr addr)
{
    return s->rtc->iomem.ops->read(s->rtc->iomem.opaque, addr, 4);
}

static void rtc_write(IbvSleepState *s, hwaddr addr, uint64_t value)
{
    s->rtc->iomem.ops->write(s->rtc->iomem.opaque, addr, value, 4);
}

/* Procesoarele pornesc din nou, dacă firmware-ul nu le ține el oprite. */
static void release_cpus(IbvSleepState *s)
{
    for (int i = 0; i < ESP32S3_CPU_COUNT; i++) {
        qemu_set_irq(s->rtc->cpu_stall_req[i], s->rtc->cpu_stall_state[i]);
    }
}

static void wake(IbvSleepState *s, uint32_t cause, uint64_t status)
{
    int64_t ms = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->slept_at) / 1000000;

    timer_del(s->settle);
    timer_del(s->alarm);
    s->asleep = false;
    esp_systimer_freeze(s->systimer, false);
    s->regs[A_SLP_WAKEUP_CAUSE / 4] = cause;
    if (cause & TRIG_EXT1) {
        s->regs[A_EXT_WAKEUP1_STATUS / 4] = status;
    }
    s->regs[A_INT_RAW / 4] |= INT_SLP_WAKEUP;
    s->regs[A_STATE0 / 4] &= ~STATE0_SLEEP_EN;
    if (cause & TRIG_EXT1) {
        ibv_journal("cip: trezit de EXT1 după %" PRId64 " ms, pinii RTC 0x%x", ms, (uint32_t)status);
    } else if (cause & TRIG_GPIO) {
        ibv_journal("cip: trezit de GPIO după %" PRId64 " ms, pinii 0x%" PRIx64, ms, status);
    } else {
        ibv_journal("cip: trezit de temporizator după %" PRId64 " ms", ms);
    }
    if (s->deep) {
        /*
         * QEMU face resetul puțin mai târziu, din bucla lui principală. Până
         * atunci procesoarele rămân oprite (le dă drumul sleep_reset_exit):
         * altfel ar mai rula câteva instrucțiuni, de fiecare dată altele, și
         * două rulări ale aceluiași scenariu n-ar mai avea aceiași timpi.
         */
        for (int i = 0; i < ESP32S3_CPU_COUNT; i++) {
            s->rtc->reset_cause[i] = ESP32_DEEPSLEEP_RESET;
        }
        s->wake_reset = true;
        qemu_irq_pulse(s->rtc->dig_reset_req);
        return;
    }
    release_cpus(s);
}

static void sleep_reset_exit(Object *obj, ResetType type)
{
    IbvSleepState *s = IBV_S3_SLEEP(obj);

    /* Registrele de aici țin de domeniul RTC: un reset al cipului nu le atinge. */
    if (s->wake_reset) {
        s->wake_reset = false;
        release_cpus(s);
    }
}

static void check_ext1(IbvSleepState *s)
{
    uint32_t ena = s->regs[A_WAKEUP_STATE / 4] >> WAKEUP_ENA_SHIFT;
    uint32_t sel = s->regs[A_EXT_WAKEUP1 / 4] & EXT_WAKEUP1_SEL;
    int want = !!(s->regs[A_EXT_WAKEUP_CONF / 4] & EXT_WAKEUP1_LV);
    uint32_t hit = 0;

    if (!s->asleep || !s->settled || !(ena & TRIG_EXT1)) {
        return;
    }
    for (int pin = 0; pin < IBV_RTC_PIN_COUNT; pin++) {
        if ((sel >> pin & 1) && ibv_pins_level(s->pins, pin) == want) {
            hit |= 1u << pin;
        }
    }
    if (hit) {
        wake(s, TRIG_EXT1, hit);
    }
}

static void check_gpio(IbvSleepState *s)
{
    uint32_t ena = s->regs[A_WAKEUP_STATE / 4] >> WAKEUP_ENA_SHIFT;
    uint64_t hit;

    if (!s->asleep || !s->settled || s->deep || !(ena & TRIG_GPIO)) {
        return;
    }
    hit = ibv_pins_gpio_wake(s->pins);
    if (hit) {
        wake(s, TRIG_GPIO, hit);
    }
}

static void check_pins(IbvSleepState *s)
{
    check_ext1(s);
    check_gpio(s);
}

static void settle_cb(void *opaque)
{
    IbvSleepState *s = opaque;

    s->settled = true;
    check_pins(s);
}

static void alarm_cb(void *opaque)
{
    IbvSleepState *s = opaque;

    if (s->asleep) {
        wake(s, TRIG_TIMER, 0);
    }
}

static void fall_asleep(IbvSleepState *s)
{
    uint32_t ena = s->regs[A_WAKEUP_STATE / 4] >> WAKEUP_ENA_SHIFT;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->deep = s->regs[A_DIG_PWC / 4] & DIG_PWC_DG_WRAP_PD_EN;
    s->asleep = true;
    s->settled = false;
    s->slept_at = now;
    for (int i = 0; i < ESP32S3_CPU_COUNT; i++) {
        qemu_set_irq(s->rtc->cpu_stall_req[i], 1);
    }
    ibv_journal("cip: somn %s, trezire: %s%s%s%s", s->deep ? "adânc" : "ușor",
                ena & TRIG_EXT1 ? "EXT1 " : "", !s->deep && (ena & TRIG_GPIO) ? "GPIO " : "",
                ena & TRIG_TIMER ? "temporizator " : "",
                ena & (TRIG_EXT1 | TRIG_TIMER) || (!s->deep && (ena & TRIG_GPIO)) ? "" : "nimic din ce știe modelul");
    if (!s->deep) {
        esp_systimer_freeze(s->systimer, true);
    }
    if (s->deep) {
        ibv_pins_digital_off(s->pins);
        if (s->regs[A_PWC / 4] & PWC_PD_EN) {
            ibv_pins_rtc_off(s->pins);
        }
    }
    if ((ena & TRIG_TIMER) && (s->regs[A_SLP_TIMER1 / 4] & SLP_TIMER1_ALARM_EN) && s->rtc->rtc_slowclk_freq) {
        uint64_t target = s->regs[A_SLP_TIMER0 / 4] | (uint64_t)(s->regs[A_SLP_TIMER1 / 4] & 0xffff) << 32;
        uint64_t ticks;

        rtc_write(s, A_TIME_UPDATE, 1u << 31);
        ticks = rtc_read(s, A_TIME_LOW0) | rtc_read(s, A_TIME_HIGH0) << 32;
        /* Ținta e un moment al contorului RTC, nu o durată de la adormire: cipul se
         * trezește atunci, doar că nu înainte să fi adormit de-a binelea. */
        timer_mod(s->alarm, now + MAX(target > ticks ? muldiv64(target - ticks, NANOSECONDS_PER_SECOND,
                                                                 s->rtc->rtc_slowclk_freq) : 0, FALL_ASLEEP_NS));
    }
    timer_mod(s->settle, now + FALL_ASLEEP_NS);
}

static uint64_t sleep_read(void *opaque, hwaddr addr, unsigned size)
{
    IbvSleepState *s = opaque;

    if (!intercepted(addr)) {
        return rtc_read(s, addr);
    }
    switch (addr) {
    case A_INT_ST:
        return s->regs[A_INT_RAW / 4] & s->regs[A_INT_ENA / 4];
    case A_INT_CLR:
    case A_INT_ENA_W1TS:
    case A_INT_ENA_W1TC:
        return 0;
    }
    return s->regs[addr / 4];
}

static void sleep_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    IbvSleepState *s = opaque;
    uint32_t v = value;

    if (!intercepted(addr)) {
        rtc_write(s, addr, value);
        return;
    }
    switch (addr) {
    case A_INT_CLR:
        s->regs[A_INT_RAW / 4] &= ~v;
        break;
    case A_INT_ENA_W1TS:
        s->regs[A_INT_ENA / 4] |= v;
        break;
    case A_INT_ENA_W1TC:
        s->regs[A_INT_ENA / 4] &= ~v;
        break;
    case A_INT_RAW:
    case A_INT_ST:
    case A_EXT_WAKEUP1_STATUS:
    case A_SLP_WAKEUP_CAUSE:
    case A_SLP_REJECT_CAUSE:
        break;    /* doar de citit */
    case A_EXT_WAKEUP1:
        s->regs[addr / 4] = v & EXT_WAKEUP1_SEL;
        if (v & EXT_WAKEUP1_STATUS_CLR) {
            s->regs[A_EXT_WAKEUP1_STATUS / 4] = 0;
        }
        break;
    case A_PAD_HOLD:
        s->regs[addr / 4] = v;
        ibv_pins_set_hold(s->pins, v);
        break;
    case A_STATE0:
        s->regs[addr / 4] = v;
        if ((v & STATE0_SLEEP_EN) && !s->asleep) {
            fall_asleep(s);
        }
        break;
    default:
        s->regs[addr / 4] = v;
    }
}

static const MemoryRegionOps sleep_ops = {
    .read = sleep_read,
    .write = sleep_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

bool ibv_sleep_is_asleep(DeviceState *dev)
{
    return IBV_S3_SLEEP(dev)->asleep;
}

void ibv_sleep_pins_changed(DeviceState *dev)
{
    check_pins(IBV_S3_SLEEP(dev));
}

static void sleep_realize(DeviceState *dev, Error **errp)
{
    IbvSleepState *s = IBV_S3_SLEEP(dev);

    if (!s->rtc || !s->pins) {
        error_setg(errp, "ibv.s3.sleep: lipsesc legăturile rtc și pins");
        return;
    }
    s->settle = timer_new_ns(QEMU_CLOCK_VIRTUAL, settle_cb, s);
    s->alarm = timer_new_ns(QEMU_CLOCK_VIRTUAL, alarm_cb, s);
}

static void sleep_init(Object *obj)
{
    IbvSleepState *s = IBV_S3_SLEEP(obj);

    memory_region_init_io(&s->iomem, obj, &sleep_ops, s, TYPE_IBV_S3_SLEEP, ESP32S3_RTC_CNTL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    object_property_add_link(obj, "rtc", TYPE_ESP32S3_RTC_CNTL, (Object **)&s->rtc,
                             qdev_prop_allow_set_link_before_realize, OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "systimer", TYPE_ESP_SYSTIMER, (Object **)&s->systimer,
                             qdev_prop_allow_set_link_before_realize, OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "pins", TYPE_IBV_S3_PINS, (Object **)&s->pins,
                             qdev_prop_allow_set_link_before_realize, OBJ_PROP_LINK_STRONG);
}

static void sleep_class_init(ObjectClass *klass, void *data)
{
    DEVICE_CLASS(klass)->realize = sleep_realize;
    RESETTABLE_CLASS(klass)->phases.exit = sleep_reset_exit;
}

static const TypeInfo sleep_info = {
    .name = TYPE_IBV_S3_SLEEP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbvSleepState),
    .instance_init = sleep_init,
    .class_init = sleep_class_init,
};

static void sleep_register_types(void)
{
    type_register_static(&sleep_info);
}

type_init(sleep_register_types)
