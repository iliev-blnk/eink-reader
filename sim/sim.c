// Renders device screens to PBM files (black = 1 in PBM, so the panel's
// white = 1 bytes are inverted on write). Convert with: magick out.pbm out.png
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "screen_bringup.h"

#define W 240
#define H 416

static int write_pbm(const gfx_t *g, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fprintf(f, "P4\n%d %d\n", g->width, g->height);
    for (int i = 0; i < g->stride * g->height; i++) fputc(~g->buf[i] & 0xFF, f);
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    const char *outdir = argc > 1 ? argv[1] : ".";
    static uint8_t buf[GFX_BUF_SIZE(W, H)];
    gfx_t g;
    gfx_init(&g, buf, W, H);
    char path[512];

    // What a normal boot shows, then the longest strings the firmware can
    // produce, to check that nothing runs off the 240 px width.
    const bringup_info_t screens[] = {
        {
            .board = "devkit-proto",
            .chip = "ESP32-S3 v0.2, 2 cores",
            .memory = "16M flash, 8M PSRAM",
            .sd = "OK, 15.9 GB, 3 files",
            .battery = "3.92 V",
            .wake = "power-on · boot 1",
            .last_button = "NEXT",
            .counter = 3,
            .last_timing = "prev: partial 412 ms",
        },
        {
            .board = "devkit-proto",
            .chip = "ESP32-S3 v0.2, 2 cores",
            .memory = "16M flash, no PSRAM",
            .sd = "not FAT32 formatted",
            .battery = "none (0.13 V)",
            .wake = "other reset · boot 1234",
            .last_button = "PREV long",
            .counter = -1234,
            .last_timing = "prev: partial 1234 ms",
        },
    };
    const char *names[] = {"bringup", "bringup-long"};
    for (int i = 0; i < 2; i++) {
        screen_bringup_draw(&g, &screens[i]);
        snprintf(path, sizeof path, "%s/%s.pbm", outdir, names[i]);
        if (write_pbm(&g, path)) return 1;
    }

    screen_sleep_draw(&g);
    snprintf(path, sizeof path, "%s/sleep.pbm", outdir);
    return write_pbm(&g, path);
}
