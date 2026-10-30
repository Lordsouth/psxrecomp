/* Compiled-image divergence at game start (dirty_ram_clear_image_baseline).
 *
 * When the boot EXE the BIOS loads differs from the EXE the code was compiled
 * from, every text page whose live bytes differ must stay dirty (its native
 * bodies are stale) and every identical page must be cleared (native). This
 * drives the exact per-page predicate memory.c uses.
 *
 * Always runs synthetic cases. Given two PS-X EXE files it also reports the real
 * result:  exe_divergence_pages_test <compiled.exe> <loaded.exe> [text_size]
 * text_size defaults to the compiled header's t_size (game.toml may lower it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dirty_ram_interp.h"

#define RAM_BYTES   (2u * 1024u * 1024u)
#define PAGE        0x1000u
#define KERNEL_END  0x10000u

static int failures = 0;
#define CHECK(c) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* Pages of [KERNEL_END, floor) the baseline keeps dirty; also verifies every
 * differing compiled-text byte below floor lands in one of them. */
static unsigned diverged_pages(const uint8_t *ram, const uint8_t *ref,
                               uint32_t ref_lo, uint32_t ref_hi,
                               uint32_t floor, int print) {
    unsigned n = 0;
    for (uint32_t lo = KERNEL_END; lo < floor; lo += PAGE) {
        uint32_t hi = lo + PAGE > floor ? floor : lo + PAGE;
        int d = dirty_ram_range_diverges_from_image(ram, ref, ref_lo, ref_hi,
                                                    lo, hi);
        for (uint32_t a = lo; a < hi; a++)
            if (a >= ref_lo && a < ref_hi && ram[a] != ref[a - ref_lo])
                CHECK(d);  /* a differing byte on a page left clean */
        if (d) {
            if (print) printf("  dirty page 0x%08X\n", 0x80000000u | lo);
            n++;
        }
    }
    return n;
}

static void synthetic(void) {
    static uint8_t ram[RAM_BYTES];
    static uint8_t ref[0x5000];
    const uint32_t ref_lo = 0x20800, ref_hi = ref_lo + sizeof(ref);
    const uint32_t floor = 0x24800;
    for (uint32_t i = 0; i < sizeof(ref); i++) ref[i] = (uint8_t)(i * 7u + 3u);
    memset(ram, 0xEE, sizeof(ram));
    memcpy(ram + ref_lo, ref, sizeof(ref));

    CHECK(diverged_pages(ram, ref, ref_lo, ref_hi, floor, 0) == 0);
    /* Bytes outside the reference are not compiled text. */
    ram[0x20000] ^= 1; ram[ref_hi + 4] ^= 1;
    CHECK(diverged_pages(ram, ref, ref_lo, ref_hi, floor, 0) == 0);
    /* At/above the floor is overlay region: never counted. */
    ram[floor] ^= 1;
    CHECK(diverged_pages(ram, ref, ref_lo, ref_hi, floor, 0) == 0);
    /* One byte in the first (partial) page and one in the last below floor. */
    ram[ref_lo] ^= 0x80;
    ram[floor - 1] ^= 0x80;
    CHECK(diverged_pages(ram, ref, ref_lo, ref_hi, floor, 0) == 2);
    CHECK(dirty_ram_range_diverges_from_image(ram, ref, ref_lo, ref_hi,
                                              0x20000, 0x21000));
    CHECK(!dirty_ram_range_diverges_from_image(ram, ref, ref_lo, ref_hi,
                                               0x21000, 0x22000));
    CHECK(!dirty_ram_range_diverges_from_image(ram, ref, ref_lo, ref_hi,
                                               0x10000, 0x20800));
}

typedef struct { uint8_t *img; uint32_t len, pc0, t_addr, t_size; } Exe;

static int load_exe(const char *path, Exe *e) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = sz > 2048 ? (uint8_t *)malloc((size_t)sz) : NULL;
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz ||
        memcmp(buf, "PS-X EXE", 8) != 0) {
        fclose(f); free(buf);
        fprintf(stderr, "%s is not a PS-X EXE\n", path);
        return 0;
    }
    fclose(f);
#define LE32(o) ((uint32_t)buf[o] | (uint32_t)buf[o+1] << 8 | \
                 (uint32_t)buf[o+2] << 16 | (uint32_t)buf[o+3] << 24)
    e->pc0 = LE32(0x10); e->t_addr = LE32(0x18); e->t_size = LE32(0x1C);
#undef LE32
    e->img = buf + 2048;
    e->len = (uint32_t)sz - 2048u;
    return 1;
}

static void place(uint8_t *ram, const Exe *e) {
    uint32_t lo = e->t_addr & 0x1FFFFFFFu, n = e->len;
    if (n > RAM_BYTES - lo) n = RAM_BYTES - lo;
    memcpy(ram + lo, e->img, n);
}

/* The modded EXE's measured entry stub (three word-copy loops, then jr to the
 * compiled entry). Applied only when the loaded pc0 is that stub. */
static void run_dw15_stub(uint8_t *ram) {
    static const uint32_t copies[3][3] = {   /* src, dst, words */
        {0x135100u, 0x1F0000u, 0x2800u},
        {0x13F100u, 0x1FA000u, 0x0BC0u},
        {0x142000u, 0x089000u, 0x1E00u},     /* below t_addr: no compiled body */
    };
    for (int c = 0; c < 3; c++)
        for (uint32_t w = 0; w < copies[c][2]; w++)
            memcpy(ram + copies[c][1] + w * 4u,
                   ram + copies[c][0] + w * 4u, 4u);
}

static int real_images(const char *compiled_path, const char *loaded_path,
                       const char *text_size_arg) {
    static uint8_t ram[RAM_BYTES];
    Exe ce, le;
    if (!load_exe(compiled_path, &ce) || !load_exe(loaded_path, &le)) return 0;
    const uint32_t ref_lo = ce.t_addr & 0x1FFFFFFFu;
    const uint32_t ref_hi = ref_lo + ce.len;
    const uint32_t text_size = text_size_arg
        ? (uint32_t)strtoul(text_size_arg, NULL, 0) : ce.t_size;
    const uint32_t floor = ref_lo + text_size;   /* compiled text end */

    printf("compiled: pc0 %08X t_addr %08X t_size %06X; floor %05X\n",
           ce.pc0, ce.t_addr, ce.t_size, floor);
    printf("loaded:   pc0 %08X t_addr %08X t_size %06X\n",
           le.pc0, le.t_addr, le.t_size);

    memset(ram, 0, sizeof(ram));
    place(ram, &ce);
    unsigned same = diverged_pages(ram, ce.img, ref_lo, ref_hi, floor, 0);
    printf("compiled EXE loaded: %u diverging page(s)\n", same);
    CHECK(same == 0);

    /* Runs of differing bytes between the two files, inside the reference. */
    unsigned runs = 0, runs_below = 0;
    uint32_t cmp = ce.len < le.len ? ce.len : le.len;
    for (uint32_t i = 0; i < cmp; i++)
        if (ce.img[i] != le.img[i] && (i == 0 || ce.img[i-1] == le.img[i-1])) {
            runs++;
            if (ref_lo + i < floor) runs_below++;
        }
    printf("file diff: %u run(s) in the compiled range, %u below the floor\n",
           runs, runs_below);

    memset(ram, 0, sizeof(ram));
    place(ram, &le);
    unsigned file_only = diverged_pages(ram, ce.img, ref_lo, ref_hi, floor, 0);
    printf("loaded EXE before entry stub: %u diverging page(s)\n", file_only);
    if (le.pc0 == 0x80135000u) {
        run_dw15_stub(ram);
        printf("loaded EXE after entry stub (state at entry_pc %08X):\n",
               ce.pc0);
        unsigned at_entry = diverged_pages(ram, ce.img, ref_lo, ref_hi, floor, 1);
        printf("  => %u of %u text page(s) stay dirty, %u run native\n",
               at_entry, (floor - KERNEL_END) / PAGE,
               (floor - KERNEL_END) / PAGE - at_entry);
        CHECK(at_entry == file_only);
        /* Code the stub copied under t_addr is outside the reference: never
         * "diverged compiled text", i.e. never given a native body. */
        CHECK(!dirty_ram_range_diverges_from_image(ram, ce.img, ref_lo, ref_hi,
                                                   0x89000u, ref_lo));
        printf("  stub code at 0x80089000..0x%08X: outside the compiled image "
               "(interpreted on first execution)\n", 0x80000000u | ref_lo);
        /* entry_pc itself is still the compiled entry: it may run native or
         * interpreted depending on its own page; report which. */
        uint32_t ep = ce.pc0 & 0x1FFFFFFFu & ~(PAGE - 1u);
        printf("  entry page 0x%08X: %s\n", 0x80000000u | ep,
               dirty_ram_range_diverges_from_image(ram, ce.img, ref_lo, ref_hi,
                                                   ep, ep + PAGE)
                   ? "dirty (interpreted)" : "clean (native)");
    }
    return 1;
}

int main(int argc, char **argv) {
    synthetic();
    if (argc >= 3 && !real_images(argv[1], argv[2], argc >= 4 ? argv[3] : NULL))
        failures++;
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("exe_divergence_pages_test: OK\n");
    return 0;
}
