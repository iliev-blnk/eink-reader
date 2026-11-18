/*
 * Emulatorul cititorului IBV: placa.
 *
 * Mașina „ibv-reader” e ESP32-S3 din QEMU plus ce e legat de el pe prototip
 * (firmware/boards/include/board_devkit.h): panoul e-paper și cardul SD pe
 * SPI2, trei butoane la masă și divizorul bateriei pe ADC. Firmware-ul rulează
 * neschimbat, același binar care se scrie în cip.
 *
 * Tot aici e scenariul de apăsări, cu aceeași sintaxă ca `sim ui`:
 *
 *   -M ibv-reader,keys="m n n M",out=DOSAR,battery=3900,hold=m
 *
 *   n p m     ÎNAINTE, ÎNAPOI, MENIU, apăsare scurtă
 *   N P M     aceleași, apăsare lungă
 *   i         așteaptă până se liniștește dispozitivul
 *   t:SEC     trec SEC secunde fără apăsări
 *   b:MV      bateria are acum MV milivolți
 *
 * `hold` ține butoanele date apăsate de la pornire până la primul ecran (așa
 * se intră în aplicația de punere în funcțiune: MENIU ținut la pornire).
 *
 * După fiecare apăsare se așteaptă liniștea: procesoarele dorm, panoul e
 * liber și pe SPI nu mai trece nimic. La sfârșitul scenariului QEMU se
 * oprește. Cadrele ajung în DOSAR/NNN.pbm, iar jurnalul în DOSAR/jurnal.txt.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/core/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/sd/sd.h"
#include "hw/misc/esp32s3_reg.h"
#include "exec/address-spaces.h"
#include "sysemu/block-backend.h"
#include "sysemu/runstate.h"
#include "ibv.h"

/* Pinii prototipului. */
#define PIN_SPI_SCK 12
#define PIN_SPI_MOSI 11
#define PIN_EPD_CS 10
#define PIN_EPD_DC 9
#define PIN_EPD_RST 8
#define PIN_EPD_BUSY 18
#define PIN_SD_CS 14
#define PIN_BTN_PREV 4
#define PIN_BTN_NEXT 5
#define PIN_BTN_MENU 6
#define BAT_ADC_CHANNEL 0     /* GPIO1 */
#define BAT_DIVIDER 2

#define SD_DRIVE_ID "ibvsd"

/* Sursele de întrerupere din matricea ESP32-S3 (soc/interrupts.h din ESP-IDF). */
#define INTR_SOURCE_GPIO 16
#define INTR_SOURCE_SPI2 21

/* Scenariul. */
#define TICK_NS 970000        /* nu 1 ms, ca să nu bată pasul cu tactul FreeRTOS */
#define BLOCK_SAMPLES 100
#define BLOCK_ACTIVE_MAX 10
#define QUIET_BLOCKS 3
#define SHORT_PRESS_MS 100
#define LONG_PRESS_MS 1000
#define IDLE_TIMEOUT_S 180

typedef enum {
    ST_OFF,
    ST_WAIT_IDLE,     /* așteaptă liniștea, apoi trece la următorul termen */
    ST_HOLD,          /* butonul e apăsat până la `until` */
    ST_TIME,          /* t:SEC */
    ST_DONE,
} ScriptState;

static struct {
    char *keys, *out, *hold;
    int battery_mv;

    DeviceState *pins, *sens, *gpspi, *panel, *sleep;
    qemu_irq sd_cs;
    bool routes_checked;

    QEMUTimer *tick;
    char **tokens;
    int tok;
    ScriptState state;
    int64_t until, waiting_since;
    int pressed;
    int samples, active, quiet;
    uint64_t last_xfers;
} b = {.battery_mv = 3900, .pressed = -1};

static void (*parent_init)(MachineState *machine);

/* ---- legăturile de pe placă ---- */

static void sd_cs_changed(void *opaque, int n, int level)
{
    qemu_set_irq(b.sd_cs, level);
    /* MISO e al cardului: panoul n-are ieșire de date. */
    ibv_gpspi_set_miso_driven(b.gpspi, !level);
}

static void pins_changed(void *opaque)
{
    ibv_sleep_pins_changed(b.sleep);
}

/* La prima tranzacție: ceasul și datele chiar ies pe pinii de pe placă? */
static void spi_started(void *opaque)
{
    uint32_t sck = ibv_pins_route(b.pins, PIN_SPI_SCK), mosi = ibv_pins_route(b.pins, PIN_SPI_MOSI);

    if (b.routes_checked) {
        return;
    }
    b.routes_checked = true;
    if (sck != 0x1004 && sck != 101) {
        ibv_warn("SPI2: ceasul nu iese pe GPIO%d (ruta 0x%x)", PIN_SPI_SCK, sck);
    }
    if (mosi != 0x1004 && mosi != 103) {
        ibv_warn("SPI2: datele nu ies pe GPIO%d (ruta 0x%x)", PIN_SPI_MOSI, mosi);
    }
}

static void set_battery(int mv)
{
    b.battery_mv = mv;
    ibv_sens_set_mv(b.sens, BAT_ADC_CHANNEL, mv / BAT_DIVIDER);
}

/* ---- scenariul ---- */

static bool sample_active(void)
{
    uint64_t xfers = ibv_gpspi_xfer_count(b.gpspi);
    bool active = xfers != b.last_xfers || ibv_uc8253_is_busy(b.panel);
    CPUState *cs;

    b.last_xfers = xfers;
    /*
     * În somn procesoarele stau, dar asta nu înseamnă liniște: în somnul ușor
     * firmware-ul poate aștepta chiar panoul, care încă desenează.
     */
    if (ibv_sleep_is_asleep(b.sleep)) {
        return active;
    }
    CPU_FOREACH(cs) {
        active |= !cs->halted;
    }
    return active;
}

static void wait_idle(void)
{
    b.state = ST_WAIT_IDLE;
    b.samples = b.active = b.quiet = 0;
    b.waiting_since = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void finish(void)
{
    b.state = ST_DONE;
    ibv_journal("gata: %d cadre, %d avertismente", ibv_uc8253_frame_count(b.panel), ibv_warn_count());
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
}

static const char button_names[] = "pnm";
static const int button_pins[] = {PIN_BTN_PREV, PIN_BTN_NEXT, PIN_BTN_MENU};

/* Butoanele din `hold`: apăsate (down) sau lăsate. */
static void set_held(bool down)
{
    for (const char *c = b.hold; c && *c; c++) {
        const char *k = strchr(button_names, *c | 0x20);

        if (!k) {
            error_report("ibv: buton necunoscut în hold: „%c”", *c);
            exit(2);
        }
        ibv_pins_set_ext(b.pins, button_pins[k - button_names], down ? 0 : -1);
    }
}

static void next_token(int64_t now)
{
    const char *names = button_names;
    const int *pins = button_pins;
    const char *tok = b.tokens ? b.tokens[b.tok] : NULL;
    const char *k;

    if (b.hold) {
        /* Primul ecran e gata: butoanele ținute de la pornire se lasă. */
        ibv_journal("scenariu: butoanele ținute la pornire (%s) sunt lăsate", b.hold);
        set_held(false);
        g_clear_pointer(&b.hold, g_free);
        wait_idle();
        return;
    }
    if (!tok) {
        finish();
        return;
    }
    b.tok++;
    if (!strncmp(tok, "t:", 2)) {
        ibv_journal("scenariu: %s", tok);
        b.state = ST_TIME;
        b.until = now + (int64_t)atoi(tok + 2) * NANOSECONDS_PER_SECOND;
        return;
    }
    if (!strncmp(tok, "b:", 2)) {
        ibv_journal("scenariu: %s", tok);
        set_battery(atoi(tok + 2));
        next_token(now);
        return;
    }
    if (!strcmp(tok, "i")) {
        ibv_journal("scenariu: i");
        wait_idle();
        return;
    }
    k = tok[1] ? NULL : strchr(names, tok[0] | 0x20);
    if (!k || !*k) {
        error_report("ibv: tastă necunoscută „%s”", tok);
        exit(2);
    }
    ibv_journal("scenariu: %s%s", tok, ibv_sleep_is_asleep(b.sleep) ? " (dispozitivul doarme)" : "");
    b.pressed = pins[k - names];
    ibv_pins_set_ext(b.pins, b.pressed, 0);
    b.state = ST_HOLD;
    b.until = now + (tok[0] >= 'a' ? SHORT_PRESS_MS : LONG_PRESS_MS) * (int64_t)SCALE_MS;
}

static void tick(void *opaque)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    bool active = sample_active();

    switch (b.state) {
    case ST_WAIT_IDLE:
        b.active += active;
        if (++b.samples == BLOCK_SAMPLES) {
            b.quiet = b.active <= BLOCK_ACTIVE_MAX ? b.quiet + 1 : 0;
            b.samples = b.active = 0;
        }
        if (b.quiet >= QUIET_BLOCKS) {
            next_token(now);
        } else if (now - b.waiting_since > IDLE_TIMEOUT_S * NANOSECONDS_PER_SECOND) {
            ibv_warn("scenariu: dispozitivul nu s-a liniștit în %d s, se merge mai departe", IDLE_TIMEOUT_S);
            next_token(now);
        }
        break;
    case ST_HOLD:
        if (now >= b.until) {
            ibv_pins_set_ext(b.pins, b.pressed, -1);
            b.pressed = -1;
            wait_idle();
        }
        break;
    case ST_TIME:
        if (now >= b.until) {
            wait_idle();
        }
        break;
    default:
        break;
    }
    if (b.state != ST_DONE) {
        timer_mod(b.tick, now + TICK_NS);
    }
}

/* ---- mașina ---- */

static DeviceState *soc_part(const char *name)
{
    g_autofree char *path = g_strdup_printf("/machine/soc/%s", name);
    Object *obj = object_resolve_path(path, NULL);

    if (!obj) {
        error_report("ibv: nu găsesc %s în mașina esp32s3", path);
        exit(1);
    }
    return DEVICE(obj);
}

static void map(DeviceState *dev, int n, hwaddr base)
{
    /* Peste ce are QEMU la adresa asta (un model mai sărac sau nimic). */
    memory_region_add_subregion_overlap(get_system_memory(), base,
                                        sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), n), 1);
}

static void ibv_reader_init(MachineState *machine)
{
    DeviceState *intmatrix, *sd;
    SSIBus *spi;
    BlockBackend *blk;

    parent_init(machine);
    intmatrix = soc_part("intmatrix");
    ibv_set_out_dir(b.out);

    b.pins = qdev_new(TYPE_IBV_S3_PINS);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(b.pins), &error_fatal);
    map(b.pins, 0, DR_REG_GPIO_BASE);
    map(b.pins, 1, DR_REG_IO_MUX_BASE);
    map(b.pins, 2, DR_REG_RTCIO_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(b.pins), 0, qdev_get_gpio_in(intmatrix, INTR_SOURCE_GPIO));

    b.sens = qdev_new(TYPE_IBV_S3_SENS);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(b.sens), &error_fatal);
    map(b.sens, 0, DR_REG_SENS_BASE);
    set_battery(b.battery_mv);

    b.gpspi = qdev_new(TYPE_IBV_S3_GPSPI);
    object_property_set_link(OBJECT(b.gpspi), "gdma", OBJECT(soc_part("gdma")), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(b.gpspi), &error_fatal);
    map(b.gpspi, 0, DR_REG_SPI2_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(b.gpspi), 0, qdev_get_gpio_in(intmatrix, INTR_SOURCE_SPI2));
    for (int i = 0; i < IBV_GPSPI_CS_COUNT; i++) {
        qdev_connect_gpio_out_named(b.gpspi, SSI_GPIO_CS, i,
                                    qdev_get_gpio_in_named(b.pins, IBV_PINS_SIG_CS, i));
    }
    ibv_gpspi_set_start_hook(b.gpspi, spi_started, NULL);
    spi = (SSIBus *)qdev_get_child_bus(b.gpspi, "spi");

    b.sleep = qdev_new(TYPE_IBV_S3_SLEEP);
    object_property_set_link(OBJECT(b.sleep), "rtc", OBJECT(soc_part("rtc_cntl")), &error_fatal);
    object_property_set_link(OBJECT(b.sleep), "systimer", OBJECT(soc_part("systimer")), &error_fatal);
    object_property_set_link(OBJECT(b.sleep), "pins", OBJECT(b.pins), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(b.sleep), &error_fatal);
    map(b.sleep, 0, DR_REG_RTCCNTL_BASE);
    ibv_pins_set_listener(b.pins, pins_changed, NULL);

    /* Panoul: CS, DC și RST de la pini, BUSY înapoi la pin. */
    b.panel = ssi_create_peripheral(spi, TYPE_IBV_UC8253);
    qdev_connect_gpio_out_named(b.pins, IBV_PINS_PAD, PIN_EPD_CS, qdev_get_gpio_in_named(b.panel, SSI_GPIO_CS, 0));
    qdev_connect_gpio_out_named(b.pins, IBV_PINS_PAD, PIN_EPD_DC,
                                qdev_get_gpio_in_named(b.panel, IBV_UC8253_DC, 0));
    qdev_connect_gpio_out_named(b.pins, IBV_PINS_PAD, PIN_EPD_RST,
                                qdev_get_gpio_in_named(b.panel, IBV_UC8253_RST, 0));
    qdev_connect_gpio_out_named(b.panel, IBV_UC8253_BUSY, 0,
                                qdev_get_gpio_in_named(b.pins, IBV_PINS_EXT, PIN_EPD_BUSY));

    /* Cardul SD: CS-ul lui e un GPIO obișnuit, cu rezistența de pull-up de pe modul. */
    sd = qdev_new("ssi-sd");
    qdev_prop_set_uint8(sd, "cs", 1);    /* doar numărul de pe magistrala QEMU; CS-ul adevărat e GPIO-ul */
    ssi_realize_and_unref(sd, spi, &error_fatal);
    b.sd_cs = qdev_get_gpio_in_named(sd, SSI_GPIO_CS, 0);
    ibv_pins_set_float_level(b.pins, PIN_SD_CS, 1);
    qdev_connect_gpio_out_named(b.pins, IBV_PINS_PAD, PIN_SD_CS, qemu_allocate_irq(sd_cs_changed, NULL, 0));
    blk = blk_by_name(SD_DRIVE_ID);
    if (blk) {
        DeviceState *card = qdev_new(TYPE_SD_CARD_SPI);

        qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
        qdev_realize_and_unref(card, qdev_get_child_bus(sd, "sd-bus"), &error_fatal);
    } else {
        warn_report("ibv: fără card SD (lipsește -drive ...,if=none,id=" SD_DRIVE_ID ")");
    }

    if (b.hold) {
        set_held(true);
    }
    if (b.keys || b.hold) {
        b.tokens = g_strsplit_set(g_strstrip(g_strdup(b.keys ?: "")), " \t\n", -1);
        /* Spațiile duble dau termeni goi: se sar. */
        int n = 0;
        for (int i = 0; b.tokens[i]; i++) {
            if (*b.tokens[i]) {
                b.tokens[n++] = b.tokens[i];
            }
        }
        b.tokens[n] = NULL;
        b.tick = timer_new_ns(QEMU_CLOCK_VIRTUAL, tick, NULL);
        wait_idle();    /* întâi pornirea, până la primul ecran */
        timer_mod(b.tick, TICK_NS);
    }
}

static void set_keys(Object *obj, const char *value, Error **errp)
{
    g_free(b.keys);
    b.keys = g_strdup(value);
}

static void set_out(Object *obj, const char *value, Error **errp)
{
    g_free(b.out);
    b.out = g_strdup(value);
}

static void set_hold(Object *obj, const char *value, Error **errp)
{
    g_free(b.hold);
    b.hold = *value ? g_strdup(value) : NULL;
}

static void set_battery_opt(Object *obj, const char *value, Error **errp)
{
    b.battery_mv = atoi(value);
}

static void ibv_reader_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    parent_init = mc->init;
    mc->init = ibv_reader_init;
    mc->desc = "Cititorul IBV: ESP32-S3 cu panou e-paper UC8253, card SD și butoane";
    object_class_property_add_str(oc, "keys", NULL, set_keys);
    object_class_property_set_description(oc, "keys", "scenariul de apăsări, ca la `sim ui`");
    object_class_property_add_str(oc, "out", NULL, set_out);
    object_class_property_set_description(oc, "out", "dosarul pentru cadre și jurnal");
    object_class_property_add_str(oc, "hold", NULL, set_hold);
    object_class_property_set_description(oc, "hold", "butoanele ținute apăsate la pornire (p, n, m)");
    object_class_property_add_str(oc, "battery", NULL, set_battery_opt);
    object_class_property_set_description(oc, "battery", "tensiunea bateriei în mV (implicit 3900)");
}

static const TypeInfo ibv_reader_info = {
    .name = MACHINE_TYPE_NAME("ibv-reader"),
    .parent = MACHINE_TYPE_NAME("esp32s3"),
    .class_init = ibv_reader_class_init,
};

static void ibv_reader_register_types(void)
{
    type_register_static(&ibv_reader_info);
}

type_init(ibv_reader_register_types)
