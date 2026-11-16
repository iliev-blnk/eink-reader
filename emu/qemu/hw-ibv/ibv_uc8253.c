/*
 * Emulatorul cititorului IBV: panoul e-paper de 3,7" cu controler UC8253.
 *
 * Un model de comportament, nu de electroni: ține cele două RAM-uri de
 * imagine, fereastra parțială, alimentarea, somnul adânc și linia BUSY cu
 * timpii obișnuiți ai panoului. După fiecare reîmprospătare scrie ce s-ar
 * vedea pe ecran într-un fișier PBM.
 *
 * Forma de undă parțială e diferențială: mișcă doar pixelii la care RAM-ul
 * „vechi” diferă de cel „nou”. Dacă firmware-ul lasă „vechiul” în urmă,
 * imaginea de aici rămâne cu urme, exact ca pe panoul adevărat.
 *
 * Ce nu se poate afla fără panou și e presupus aici: resetul software nu
 * șterge RAM-urile; resetul hardware și somnul adânc le fac de nefolosit.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "ibv.h"

#define STRIDE (IBV_EPD_WIDTH / 8)
#define FB_SIZE (STRIDE * IBV_EPD_HEIGHT)

#define CMD_PSR 0x00
#define CMD_POF 0x02
#define CMD_PON 0x04
#define CMD_DSLP 0x07
#define CMD_DTM1 0x10
#define CMD_DRF 0x12
#define CMD_DTM2 0x13
#define CMD_CDI 0x50
#define CMD_PTL 0x90
#define CMD_PTIN 0x91
#define CMD_PTOUT 0x92
#define CMD_CCSET 0xE0
#define CMD_TSSET 0xE5
#define CMD_NONE 0x100

#define CDI_DEFAULT 0xD7
#define CDI_DDX0 0x10    /* 1 în RAM înseamnă alb */

/* Timpii BUSY, în microsecunde. */
#define T_RESET_US 1000
#define T_POWER_US 45000
#define T_FULL_US 3000000
#define T_FAST_FULL_US 1000000
#define T_PARTIAL_US 350000

#define TS_FAST_FULL 0x5A
#define TS_PARTIAL 0x6E

typedef struct IbvUc8253State {
    SSIPeripheral parent_obj;
    qemu_irq busy;
    QEMUTimer *timer;

    bool dc, rst_low, busy_low, deep_sleep, power_on, partial, ts_fix;
    bool old_valid, new_valid;
    bool refreshing, refresh_diff;
    int rx0, ry0, rx1, ry1;       /* zona reîmprospătării în curs */
    int64_t refresh_us;
    uint32_t cmd, n;              /* comanda curentă și câți octeți de date a primit */
    uint8_t cdi, ts;
    uint8_t ptl[7];
    int wx0, wy0, wx1, wy1;       /* fereastra parțială, cu marginile incluse */

    uint8_t ram_old[FB_SIZE], ram_new[FB_SIZE];
    uint8_t shown[FB_SIZE];       /* ce e pe ecran; bitul 1 = alb */
    int frames;
} IbvUc8253State;

OBJECT_DECLARE_SIMPLE_TYPE(IbvUc8253State, IBV_UC8253)

static void go_busy(IbvUc8253State *s, int64_t us)
{
    s->busy_low = true;
    qemu_set_irq(s->busy, 0);
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + us * 1000);
}

static void chip_reset(IbvUc8253State *s, bool hard)
{
    s->cdi = CDI_DEFAULT;
    s->ts_fix = false;
    s->partial = false;
    s->power_on = false;
    s->wx0 = s->wy0 = 0;
    s->wx1 = IBV_EPD_WIDTH - 1;
    s->wy1 = IBV_EPD_HEIGHT - 1;
    if (hard) {
        s->cmd = CMD_NONE;
        s->deep_sleep = false;
        s->old_valid = s->new_valid = false;
    }
}

static void dump_frame(IbvUc8253State *s)
{
    g_autofree char *path = NULL;
    FILE *f;

    if (!ibv_out_dir()) {
        return;
    }
    path = g_strdup_printf("%s/%03d.pbm", ibv_out_dir(), s->frames);
    f = fopen(path, "wb");
    if (!f) {
        ibv_warn("nu pot scrie %s", path);
        return;
    }
    fprintf(f, "P4\n%d %d\n", IBV_EPD_WIDTH, IBV_EPD_HEIGHT);
    for (int i = 0; i < FB_SIZE; i++) {
        fputc(~s->shown[i] & 0xff, f);    /* în PBM 1 e negru */
    }
    fclose(f);
}

static void finish_refresh(IbvUc8253State *s)
{
    uint8_t flip = s->cdi & CDI_DDX0 ? 0 : 0xff;

    for (int y = s->ry0; y <= s->ry1; y++) {
        for (int bx = s->rx0 / 8; bx <= s->rx1 / 8; bx++) {
            int i = y * STRIDE + bx;
            uint8_t want = s->ram_new[i] ^ flip;

            if (s->refresh_diff) {
                uint8_t moved = s->ram_old[i] ^ s->ram_new[i];

                s->shown[i] = (s->shown[i] & ~moved) | (want & moved);
            } else {
                s->shown[i] = want;
            }
        }
    }
    s->frames++;    /* numerotate de la 001, ca la simulator */
    dump_frame(s);
    ibv_journal("panou: cadrul %03d, %s, zona %d,%d %dx%d, %" PRId64 " ms", s->frames,
                s->refresh_diff ? "parțial" : s->refresh_us == T_FAST_FULL_US ? "complet rapid" : "complet",
                s->rx0, s->ry0, s->rx1 - s->rx0 + 1, s->ry1 - s->ry0 + 1, s->refresh_us / 1000);
    s->refreshing = false;
}

static void busy_done(void *opaque)
{
    IbvUc8253State *s = opaque;

    if (s->rst_low) {
        return;
    }
    if (s->refreshing) {
        finish_refresh(s);
    }
    s->busy_low = false;
    qemu_set_irq(s->busy, 1);
}

static void start_refresh(IbvUc8253State *s)
{
    if (!s->power_on) {
        ibv_warn("panou: DRF cu alimentarea oprită, nu se întâmplă nimic");
        return;
    }
    s->refresh_diff = s->ts_fix && s->ts == TS_PARTIAL;
    s->refresh_us = s->refresh_diff ? T_PARTIAL_US
                    : s->ts_fix && s->ts == TS_FAST_FULL ? T_FAST_FULL_US : T_FULL_US;
    if (s->partial) {
        s->rx0 = s->wx0;
        s->ry0 = s->wy0;
        s->rx1 = s->wx1;
        s->ry1 = s->wy1;
    } else {
        s->rx0 = s->ry0 = 0;
        s->rx1 = IBV_EPD_WIDTH - 1;
        s->ry1 = IBV_EPD_HEIGHT - 1;
    }
    if (!s->new_valid || (s->refresh_diff && !s->old_valid)) {
        ibv_warn("panou: reîmprospătare cu RAM-ul de imagine nescris de la ultimul reset");
    }
    s->refreshing = true;
    go_busy(s, s->refresh_us);
}

/* Verifică, la următoarea comandă, că cea dinainte și-a primit toate datele. */
static void end_command(IbvUc8253State *s)
{
    if (s->cmd == CMD_DTM1 || s->cmd == CMD_DTM2) {
        uint32_t want = (s->wx1 / 8 - s->wx0 / 8 + 1) * (s->wy1 - s->wy0 + 1);
        bool whole = s->wx0 == 0 && s->wy0 == 0 && s->wx1 == IBV_EPD_WIDTH - 1 && s->wy1 == IBV_EPD_HEIGHT - 1;

        if (s->n != want) {
            ibv_warn("panou: comanda 0x%02x a primit %u octeți, fereastra are %u", s->cmd, s->n, want);
        } else if (whole) {
            *(s->cmd == CMD_DTM1 ? &s->old_valid : &s->new_valid) = true;
        }
    } else if (s->cmd == CMD_PTL && s->n != 7) {
        ibv_warn("panou: PTL cu %u octeți în loc de 7", s->n);
    } else if (s->cmd == CMD_DSLP && !s->deep_sleep) {
        ibv_warn("panou: DSLP fără codul 0xA5");
    }
}

static void command(IbvUc8253State *s, uint8_t c)
{
    if (s->deep_sleep) {
        ibv_warn("panou: comanda 0x%02x în somn adânc, ignorată (trebuie un puls pe RST)", c);
        return;
    }
    end_command(s);
    if (s->busy_low) {
        ibv_warn("panou: comanda 0x%02x cu BUSY jos", c);
    }
    s->cmd = c;
    s->n = 0;

    switch (c) {
    case CMD_PON:
        s->power_on = true;
        go_busy(s, T_POWER_US);
        break;
    case CMD_POF:
        s->power_on = false;
        go_busy(s, T_POWER_US);
        break;
    case CMD_DRF:
        start_refresh(s);
        break;
    case CMD_PTIN:
        s->partial = true;
        break;
    case CMD_PTOUT:
        s->partial = false;
        s->wx0 = s->wy0 = 0;
        s->wx1 = IBV_EPD_WIDTH - 1;
        s->wy1 = IBV_EPD_HEIGHT - 1;
        break;
    case CMD_PSR:
    case CMD_DSLP:
    case CMD_DTM1:
    case CMD_DTM2:
    case CMD_CDI:
    case CMD_PTL:
    case CMD_CCSET:
    case CMD_TSSET:
        break;
    default:
        ibv_warn("panou: comanda 0x%02x nu e în model", c);
    }
}

static void ram_byte(IbvUc8253State *s, uint8_t *ram, uint8_t d)
{
    int bw = s->wx1 / 8 - s->wx0 / 8 + 1, rows = s->wy1 - s->wy0 + 1;

    if (s->n >= (uint32_t)(bw * rows)) {
        return;    /* prea multe date: se numără, iar end_command avertizează */
    }
    ram[(s->wy0 + s->n / bw) * STRIDE + s->wx0 / 8 + s->n % bw] = d;
}

static void data(IbvUc8253State *s, uint8_t d)
{
    if (s->deep_sleep || s->cmd == CMD_NONE) {
        return;
    }
    switch (s->cmd) {
    case CMD_PSR:
        if (s->n == 0 && !(d & 1)) {
            chip_reset(s, false);
            go_busy(s, T_RESET_US);
        }
        break;
    case CMD_DSLP:
        if (s->n == 0 && d == 0xA5) {
            s->deep_sleep = true;
            s->old_valid = s->new_valid = false;
            ibv_journal("panou: somn adânc");
        }
        break;
    case CMD_DTM1:
        ram_byte(s, s->ram_old, d);
        break;
    case CMD_DTM2:
        ram_byte(s, s->ram_new, d);
        break;
    case CMD_CDI:
        if (s->n == 0) {
            s->cdi = d;
        }
        break;
    case CMD_PTL:
        if (s->n < 7) {
            s->ptl[s->n] = d;
        }
        if (s->n == 6) {
            int x0 = s->ptl[0] & 0xf8, x1 = s->ptl[1] | 7;
            int y0 = s->ptl[2] << 8 | s->ptl[3], y1 = s->ptl[4] << 8 | s->ptl[5];

            if (x0 > x1 || x1 >= IBV_EPD_WIDTH || y0 > y1 || y1 >= IBV_EPD_HEIGHT) {
                ibv_warn("panou: fereastră greșită %d..%d x %d..%d", x0, x1, y0, y1);
            } else {
                s->wx0 = x0;
                s->wx1 = x1;
                s->wy0 = y0;
                s->wy1 = y1;
            }
        }
        break;
    case CMD_CCSET:
        if (s->n == 0) {
            s->ts_fix = d & 0x02;
        }
        break;
    case CMD_TSSET:
        if (s->n == 0) {
            s->ts = d;
        }
        break;
    }
    s->n++;
}

static uint32_t uc8253_transfer(SSIPeripheral *dev, uint32_t tx)
{
    IbvUc8253State *s = IBV_UC8253(dev);

    if (!s->rst_low) {
        if (s->dc) {
            data(s, tx);
        } else {
            command(s, tx);
        }
    }
    return 0;    /* modulul n-are MISO */
}

static void dc_in(void *opaque, int n, int level)
{
    IbvUc8253State *s = opaque;

    s->dc = level;
}

static void rst_in(void *opaque, int n, int level)
{
    IbvUc8253State *s = opaque;
    bool low = !level;

    if (low == s->rst_low) {
        return;
    }
    s->rst_low = low;
    if (low) {
        if (s->refreshing) {
            ibv_warn("panou: reset în mijlocul unei reîmprospătări");
            s->refreshing = false;
        }
        ibv_journal("panou: reset hardware%s", s->deep_sleep ? ", iese din somnul adânc" : "");
        chip_reset(s, true);
        timer_del(s->timer);
        s->busy_low = true;
        qemu_set_irq(s->busy, 0);
    } else {
        go_busy(s, T_RESET_US);
    }
}

bool ibv_uc8253_is_busy(DeviceState *dev)
{
    IbvUc8253State *s = IBV_UC8253(dev);

    return s->busy_low && !s->rst_low;
}

int ibv_uc8253_frame_count(DeviceState *dev)
{
    return IBV_UC8253(dev)->frames;
}

/*
 * Panoul are alimentarea lui: resetul cipului nu-i schimbă starea. Doar
 * linia BUSY se anunță din nou, ca pinul să pornească de la nivelul bun.
 */
static void uc8253_reset_hold(Object *obj, ResetType type)
{
    IbvUc8253State *s = IBV_UC8253(obj);

    qemu_set_irq(s->busy, !s->busy_low);
}

static void uc8253_realize(SSIPeripheral *dev, Error **errp)
{
    IbvUc8253State *s = IBV_UC8253(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, busy_done, s);
    qdev_init_gpio_in_named(DEVICE(dev), dc_in, IBV_UC8253_DC, 1);
    qdev_init_gpio_in_named(DEVICE(dev), rst_in, IBV_UC8253_RST, 1);
    qdev_init_gpio_out_named(DEVICE(dev), &s->busy, IBV_UC8253_BUSY, 1);
    memset(s->shown, 0xff, sizeof s->shown);
    chip_reset(s, true);
}

static void uc8253_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = uc8253_realize;
    k->transfer = uc8253_transfer;
    k->cs_polarity = SSI_CS_LOW;
    RESETTABLE_CLASS(klass)->phases.hold = uc8253_reset_hold;
}

static const TypeInfo uc8253_info = {
    .name = TYPE_IBV_UC8253,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(IbvUc8253State),
    .class_init = uc8253_class_init,
};

static void uc8253_register_types(void)
{
    type_register_static(&uc8253_info);
}

type_init(uc8253_register_types)
