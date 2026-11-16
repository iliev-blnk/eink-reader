/*
 * Emulatorul cititorului IBV: jurnalul rulării.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "ibv.h"

static char *out_dir;
static FILE *journal;
static int warnings;

void ibv_set_out_dir(const char *dir)
{
    g_free(out_dir);
    out_dir = g_strdup(dir);
    if (journal) {
        fclose(journal);
        journal = NULL;
    }
    if (dir) {
        g_autofree char *path = g_strdup_printf("%s/jurnal.txt", dir);
        g_mkdir_with_parents(dir, 0755);
        journal = fopen(path, "w");
        if (!journal) {
            error_report("ibv: nu pot scrie %s", path);
            exit(1);
        }
    }
}

const char *ibv_out_dir(void)
{
    return out_dir;
}

static void G_GNUC_PRINTF(2, 0) emit(const char *tag, const char *fmt, va_list ap)
{
    g_autofree char *msg = g_strdup_vprintf(fmt, ap);
    int64_t us = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000;

    if (journal) {
        fprintf(journal, "%10.3f  %s%s\n", us / 1000.0, tag, msg);
        fflush(journal);
    }
    if (*tag) {
        fprintf(stderr, "ibv: %10.3f ms  %s%s\n", us / 1000.0, tag, msg);
    }
}

void ibv_journal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit("", fmt, ap);
    va_end(ap);
}

void ibv_warn(const char *fmt, ...)
{
    va_list ap;

    warnings++;
    va_start(ap, fmt);
    emit("ATENȚIE: ", fmt, ap);
    va_end(ap);
}

int ibv_warn_count(void)
{
    return warnings;
}
