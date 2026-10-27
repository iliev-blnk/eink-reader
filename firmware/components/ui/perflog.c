#include "perflog.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// O întoarcere de pagină face ~10 rânduri, iar indexul în fundal unul la 8
// pagini; 256 de rânduri (6 KB) ajung între două scrieri.
#define CAP 256
#define URGENT (CAP * 3 / 4)

typedef struct {
    uint64_t t_us;
    const char *event;
    uint32_t a, b;
} rec_t;

static rec_t s_rec[CAP];
static int s_count;
static uint32_t s_dropped;
static bool s_on;
static char s_path[160], s_rel[16];
static const char FLUSH[] = "perf_flush";

bool perflog_open(const char *root, uint64_t now_us)
{
    char dir[128];
    snprintf(dir, sizeof dir, "%s/perf", root);
    DIR *d = opendir(dir);
    if (!d) return false;
    int last = 0;
    for (struct dirent *e; (e = readdir(d));) {
        int n;
        char tail;
        if (sscanf(e->d_name, "%4d.csv%c", &n, &tail) == 1 && n > last) last = n;
    }
    closedir(d);
    snprintf(s_rel, sizeof s_rel, "perf/%04d.csv", (last + 1) % 10000);
    snprintf(s_path, sizeof s_path, "%s/%s", root, s_rel);
    FILE *f = fopen(s_path, "w");
    if (!f) return false;
    fputs("t_us,event,a,b\n", f);
    fclose(f);
    s_on = true;
    s_count = 0;
    perflog_add(now_us, "perf_open", (uint32_t)(last + 1), 0);
    return true;
}

bool perflog_on(void) { return s_on; }
const char *perflog_file(void) { return s_on ? s_rel : NULL; }
// Rândul `perf_flush` al scrierii de dinainte nu cere singur o scriere nouă.
int perflog_pending(void) { return s_count == 1 && s_rec[0].event == FLUSH ? 0 : s_count; }
bool perflog_urgent(void) { return s_count >= URGENT; }

void perflog_add(uint64_t t_us, const char *event, uint32_t a, uint32_t b)
{
    if (!s_on) return;
    if (s_count == CAP) {
        s_dropped++;
        return;
    }
    s_rec[s_count++] = (rec_t){t_us, event, a, b};
}

void perflog_flush(uint64_t (*now_us)(void))
{
    if (!s_on || !perflog_pending()) return;
    uint64_t t0 = now_us();
    FILE *f = fopen(s_path, "a");
    if (!f) {
        s_on = false;    // cardul a dispărut: nu mai încearcă la fiecare apăsare
        return;
    }
    int n = s_count;
    for (int i = 0; i < n; i++)
        fprintf(f, "%" PRIu64 ",%s,%" PRIu32 ",%" PRIu32 "\n", s_rec[i].t_us, s_rec[i].event, s_rec[i].a,
                s_rec[i].b);
    if (s_dropped) fprintf(f, "%" PRIu64 ",perf_dropped,%" PRIu32 ",0\n", t0, s_dropped);
    fclose(f);
    s_count = 0;
    s_dropped = 0;
    // Intră în scrierea următoare: cât a ținut asta.
    perflog_add(t0, FLUSH, (uint32_t)n, (uint32_t)(now_us() - t0));
}
