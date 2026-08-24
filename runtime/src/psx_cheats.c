/* psx_cheats.c -- GameShark/PEC-style runtime cheat engine.
 *
 * File format (see psx_cheats.h for why this is not a mod-package patch):
 *
 *   # comment
 *   [x] Best Happiness      <- enabled cheat
 *   8013848A 00AA
 *
 *   [ ] Max Bits            <- present but off
 *   80134EB8 E0FF
 *
 * Code words are `TAAAAAA VVVV`: the top byte selects the operation and the
 * low 24 bits are the RAM address.
 *
 *   30 aaaaaa 00vv   write byte
 *   80 aaaaaa vvvv   write halfword
 *   D0/D1/D2/D3      halfword ==, !=, <, >   -- gate the NEXT line
 *   E0/E1/E2/E3      byte     ==, !=, <, >   -- gate the NEXT line
 *
 * Anything else (50 repeat blocks, C0/C2 vendor extensions) is reported at
 * load time and skipped. Guessing at an unknown opcode would poke the wrong
 * addresses, and a cheat engine that silently corrupts RAM is worse than one
 * that says it does not know the code.
 */
#include "psx_cheats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <SDL3/SDL.h>

extern uint8_t  psx_read_byte(uint32_t addr);
extern uint16_t psx_read_half(uint32_t addr);
extern void     psx_write_byte(uint32_t addr, uint8_t val);
extern void     psx_write_half(uint32_t addr, uint16_t val);

#define MAX_CODES 4096
#define MAX_NAME  96
#define PSX_RAM_SIZE 0x200000u

typedef struct {
    uint8_t  op;        /* top byte of the code word */
    uint32_t addr;      /* KSEG0 address, already range-checked */
    uint16_t val;
    int      cheat;     /* index into s_names */
} CheatCode;

static CheatCode s_codes[MAX_CODES];
static int       s_code_count;

static char s_names[128][MAX_NAME];
static int  s_enabled[128];
static int  s_name_count;

static char s_path[512];
static int  s_active;          /* any enabled cheat with codes */
static int  s_loaded;

static int ram_addr(uint32_t word, uint32_t *out)
{
    uint32_t phys = word & 0x00FFFFFFu;
    if (phys >= PSX_RAM_SIZE) return 0;      /* never poke outside main RAM */
    *out = 0x80000000u | phys;
    return 1;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
                     e[-1] == '\n')) e--;
    *e = '\0';
    return s;
}

void psx_cheats_load(const char *path)
{
    s_loaded = 1;
    s_code_count = 0;
    s_name_count = 0;
    s_active = 0;

    /* Resolve in the order that matches how players actually lay this out:
     * an explicit override, the working directory, next to the exe, and the
     * project/package root one level up (build-release/.. -- the launcher and
     * the .bat both run from there). */
    FILE *f = NULL;
    if (path && path[0]) {
        snprintf(s_path, sizeof(s_path), "%s", path);
        f = fopen(s_path, "rb");
    }
    if (!f) {
        const char *env = SDL_getenv("PSX_CHEATS");
        if (env && env[0]) {
            snprintf(s_path, sizeof(s_path), "%s", env);
            f = fopen(s_path, "rb");
        }
    }
    if (!f) {
        snprintf(s_path, sizeof(s_path), "cheats.txt");
        f = fopen(s_path, "rb");
    }
    if (!f) {
        const char *base = SDL_GetBasePath();
        if (base && base[0]) {
            snprintf(s_path, sizeof(s_path), "%scheats.txt", base);
            f = fopen(s_path, "rb");
            if (!f) {
                snprintf(s_path, sizeof(s_path), "%s../cheats.txt", base);
                f = fopen(s_path, "rb");
            }
        }
    }
    if (!f) {
        /* Never fail silently: a cheat file that is simply not being found
         * looks exactly like codes that do not work. */
        fprintf(stdout,
                "psxrecomp: cheats: no cheats.txt found (cwd, exe dir, exe/..;"
                " override with PSX_CHEATS=<path>)\n");
        fflush(stdout);
        return;
    }

    int cur = -1;
    int unsupported = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (!*p || *p == '#' || *p == ';') continue;

        if (*p == '[') {
            /* [x] Name  /  [ ] Name */
            int on = (p[1] == 'x' || p[1] == 'X' || p[1] == '*');
            char *close = strchr(p, ']');
            if (!close) continue;
            char *name = trim(close + 1);
            if (s_name_count >= (int)(sizeof(s_enabled) / sizeof(s_enabled[0])))
                break;
            cur = s_name_count++;
            snprintf(s_names[cur], MAX_NAME, "%s", name);
            s_enabled[cur] = on;
            continue;
        }

        unsigned int w = 0, v = 0;
        if (sscanf(p, "%8x %4x", &w, &v) != 2) continue;
        if (cur < 0) continue;                 /* codes before any header */
        if (s_code_count >= MAX_CODES) break;

        uint8_t op = (uint8_t)(w >> 24);
        switch (op) {
        case 0x30: case 0x80:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3:
        case 0xE0: case 0xE1: case 0xE2: case 0xE3:
            break;
        case 0x50:
            /* Repeat block: the low bytes are (count, address step) and the
             * operand is the value step; it applies to the NEXT write line.
             * Semantics confirmed against published long-form equivalents --
             * `50000A01 0000 / 3013D492 0063` expands to the ten explicit
             * item-slot writes 3013D492..3013D49B. Address range is checked
             * per iteration at apply time, not here. */
            break;
        default:
            if (s_enabled[cur]) {
                fprintf(stdout,
                        "psxrecomp: cheats: '%s' uses unsupported code type "
                        "%02X (%08X %04X) -- skipped\n",
                        s_names[cur], op, w, v);
                unsupported++;
            }
            continue;
        }

        uint32_t addr = 0;
        if (op == 0x50) {
            addr = w & 0x00FFFFFFu;          /* count<<8 | step, not an address */
        } else if (!ram_addr(w, &addr)) {
            if (s_enabled[cur]) {
                fprintf(stdout,
                        "psxrecomp: cheats: '%s' targets %08X outside main RAM "
                        "-- skipped\n", s_names[cur], w & 0x00FFFFFFu);
            }
            continue;
        }

        CheatCode *c = &s_codes[s_code_count++];
        c->op = op;
        c->addr = addr;
        c->val = (uint16_t)v;
        c->cheat = cur;
    }
    fclose(f);

    int on_count = 0;
    for (int i = 0; i < s_name_count; i++) if (s_enabled[i]) on_count++;
    for (int i = 0; i < s_code_count; i++) {
        if (s_enabled[s_codes[i].cheat]) { s_active = 1; break; }
    }
    fprintf(stdout,
            "psxrecomp: cheats: %s -- %d cheat(s), %d enabled, %d code line(s)"
            "%s\n",
            s_path, s_name_count, on_count, s_code_count,
            unsupported ? " (some skipped)" : "");
    for (int i = 0; i < s_name_count; i++) {
        if (s_enabled[i]) fprintf(stdout, "psxrecomp: cheats:   ON  %s\n",
                                  s_names[i]);
    }
    fflush(stdout);
}

void psx_cheats_on_vblank(void)
{
    if (!s_loaded) psx_cheats_load(NULL);
    if (!s_active) return;

    int skip = 0;
    unsigned rep_count = 0, rep_step = 0, rep_vstep = 0;
    for (int i = 0; i < s_code_count; i++) {
        const CheatCode *c = &s_codes[i];
        if (!s_enabled[c->cheat]) { skip = 0; rep_count = 0; continue; }
        if (skip) { skip = 0; continue; }

        switch (c->op) {
        case 0x50:
            rep_count = (c->addr >> 8) & 0xFF;
            rep_step  = c->addr & 0xFF;
            rep_vstep = c->val;
            continue;                     /* arms the next write line */

        case 0x30:
            if (rep_count > 1) {
                for (unsigned r = 0; r < rep_count; r++) {
                    uint32_t a = c->addr + r * rep_step;
                    if ((a & 0x1FFFFFu) >= PSX_RAM_SIZE) break;
                    psx_write_byte(a, (uint8_t)((c->val + r * rep_vstep) & 0xFF));
                }
            } else {
                psx_write_byte(c->addr, (uint8_t)(c->val & 0xFF));
            }
            rep_count = 0;
            break;

        case 0x80:
            if (rep_count > 1) {
                for (unsigned r = 0; r < rep_count; r++) {
                    uint32_t a = c->addr + r * rep_step;
                    if ((a & 0x1FFFFFu) >= PSX_RAM_SIZE) break;
                    psx_write_half(a, (uint16_t)(c->val + r * rep_vstep));
                }
            } else {
                psx_write_half(c->addr, c->val);
            }
            rep_count = 0;
            break;

        /* A conditional gates only the following line, exactly like the
         * hardware devices these codes were written for. */
        case 0xD0: skip = !(psx_read_half(c->addr) == c->val); break;
        case 0xD1: skip = !(psx_read_half(c->addr) != c->val); break;
        case 0xD2: skip = !(psx_read_half(c->addr) <  c->val); break;
        case 0xD3: skip = !(psx_read_half(c->addr) >  c->val); break;
        case 0xE0: skip = !(psx_read_byte(c->addr) == (uint8_t)(c->val & 0xFF)); break;
        case 0xE1: skip = !(psx_read_byte(c->addr) != (uint8_t)(c->val & 0xFF)); break;
        case 0xE2: skip = !(psx_read_byte(c->addr) <  (uint8_t)(c->val & 0xFF)); break;
        case 0xE3: skip = !(psx_read_byte(c->addr) >  (uint8_t)(c->val & 0xFF)); break;
        default: break;
        }
    }
}

int psx_cheats_enabled_count(void)
{
    int n = 0;
    for (int i = 0; i < s_name_count; i++) if (s_enabled[i]) n++;
    return n;
}
