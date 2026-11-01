/* test_sw_texture_banks.c -- the software renderer draws retained texture
 * banks (mod_texture_banks.h) through the backend hook, as GL does.
 *
 * A bank holds VRAM-shaped words, so an 8bpp tile drawn from a bank must be
 * byte-identical to the same words drawn from VRAM: indices from the bank,
 * and the palette from the bank too unless the bank keeps a live palette
 * (then the VRAM CLUT applies, so a palette upload recolours it). Words past
 * the bank's extent read 0, as in the GL shader; bank texels are point-sampled even
 * with the bilinear filter on, and id 0 restores VRAM and the player's filter.
 * The facade refuses banks under the netplay CPU-authoritative raster and
 * while an HD pack is active. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/gpu_sw_renderer.c"
#include "../src/gpu_render.c"
#include "../src/mod_texture_banks.c"

int g_ws_bd_stretch_on, g_ws_bd_stretch_pct;
int psx_ws_prim_in_backdrop(void) { return 0; }
int g_psx_vram_dirty_tracking;
void gpu_vram_dirty_mark_row_impl(uint32_t y) { (void)y; }
void gpu_vram_dirty_mark_rect(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
void gpu_vram_dirty_mark_all(void) {}
const GpuRenderBackend *gl_backend_get(void) { return NULL; }
const GpuRenderBackend *vk_backend_get(void) { return NULL; }
static int hd_active;
int gpu_hd_textures_active(void) { return hd_active; }
int gpu_hd_textures_dump_enabled(void) { return 0; }
void gpu_hd_textures_set_vram(const uint16_t *v) { (void)v; }
void gpu_hd_textures_observe_draw(uint16_t tp, uint16_t cx, uint16_t cy,
                                  const int l[4], uint32_t w, int semi) {
    (void)tp; (void)cx; (void)cy; (void)l; (void)w; (void)semi;
}
void gpu_hd_textures_track_upload(int x, int y, int w, int h, const uint16_t *d) {
    (void)x; (void)y; (void)w; (void)h; (void)d;
}
void gpu_hd_textures_invalidate(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
void gpu_hd_textures_begin_upload(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
}
void gpu_hd_textures_begin_copy(int sx, int sy, int dx, int dy, int w, int h) {
    (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h;
}
void gpu_hd_textures_end_copy(void) {}
uint32_t psx_mod_gpu_dma_memory_alloc(uint32_t n, uint32_t a) { (void)n; (void)a; return 0; }
int psx_mod_host_output_scope(void) { return 0; }
uint32_t psx_mod_read_word(uint32_t a) { (void)a; return 0; }

static uint16_t vram[1024 * 512];
static uint16_t bank[1024 * 256];
static uint16_t ref[64 * 64], out[64 * 64];

/* 8bpp page at x=768 (VRAM) / slot x=64 (bank); CLUT at (0,480). */
#define TP_VRAM ((uint16_t)((1u << 7) | 12u))
#define TP_BANK ((uint16_t)((1u << 7) | 1u))

static uint16_t idx_word(int x, int y) {
    return (uint16_t)(((x * 3 + y) & 0xFF) | (((x * 5 + y * 7 + 1) & 0xFF) << 8));
}
static uint16_t colour(int i, int salt) {
    return (uint16_t)(((i + salt) & 31) | (((i >> 3) & 31) << 5) |
                      (((i * 3) & 31) << 10) | 0x8000u);
}

static void tile(uint16_t texpage, int u0, int filter, uint16_t *dst) {
    for (int y = 0; y < 64; y++) memset(vram + y * 1024, 0, 64 * 2);
    sw_set_texture_filter(filter);
    sw_set_color_modulation(16, 16, 16, 1);
    sw_draw_textured_rect(0, 0, 64, 64, u0, 0, 0, 480, texpage);
    for (int y = 0; y < 64; y++) memcpy(dst + y * 64, vram + y * 1024, 64 * 2);
}

int main(void) {
    gr_init(vram);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 64; x++) {
            vram[y * 1024 + 768 + x] = idx_word(x, y);
            bank[y * 1024 + 64 + x] = idx_word(x, y);
        }
    for (int i = 0; i < 256; i++) {
        vram[480 * 1024 + i] = colour(i, 0);
        bank[(256 - 32) * 1024 + i] = colour(i, 9);   /* the bank's own CLUT row */
    }
    /* 1 = live palette; 2 = the same words with its own palette (row 224). */
    assert(psx_mod_define_texture_bank_live_clut(1, 1024, 256, bank));
    assert(psx_mod_define_texture_bank(2, 1024, 256, bank));

    /* Reference: VRAM page and CLUT. */
    tile(TP_VRAM, 0, 0, ref);
    int lit = 0;
    for (int i = 0; i < 64 * 64; i++) lit += ref[i] != 0;
    assert(lit == 64 * 64);

    /* Live-palette bank: identical to VRAM, bilinear request ignored. */
    assert(gr_texture_banks_supported());
    assert(gr_select_texture_bank(1, mod_texture_bank_live_clut(1)));
    tile(TP_BANK, 0, 1, out);
    assert(sw_texture_filter() == 0);
    assert(memcmp(out, ref, sizeof ref) == 0);
    /* A palette upload recolours it (the palette is live VRAM). */
    vram[480 * 1024 + idx_word(0, 0) % 256] ^= 0x1F;
    tile(TP_BANK, 0, 0, out);
    assert(memcmp(out, ref, sizeof ref) != 0);
    vram[480 * 1024 + idx_word(0, 0) % 256] ^= 0x1F;
    /* Texels come from the bank: corrupting the VRAM page changes nothing. */
    vram[768] ^= 0xFFFF;
    tile(TP_BANK, 0, 0, out);
    assert(memcmp(out, ref, sizeof ref) == 0);
    vram[768] ^= 0xFFFF;

    /* Own-palette bank: CLUT read from the bank's row 224. */
    assert(gr_select_texture_bank(2, mod_texture_bank_live_clut(2)));
    sw_set_color_modulation(16, 16, 16, 1);
    for (int y = 0; y < 64; y++) memset(vram + y * 1024, 0, 64 * 2);
    sw_draw_textured_rect(0, 0, 64, 64, 0, 0, 0, 224, TP_BANK);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int u = x, w = idx_word(u / 2, y);
            int i = (u & 1) ? (w >> 8) & 0xFF : w & 0xFF;
            assert(vram[y * 1024 + x] == colour(i, 9));
        }

    /* Past the bank's extent (page y=256..) a texel word reads 0: index 0. */
    assert(gr_select_texture_bank(1, 1));
    tile((uint16_t)(TP_BANK | 0x10u), 0, 0, out);
    for (int i = 0; i < 64 * 64; i++) assert(out[i] == vram[480 * 1024]);

    /* id 0: VRAM again, and the player's filter comes back. */
    sw_set_texture_filter(1);
    assert(gr_select_texture_bank(0, 0));
    assert(sw_texture_filter() == 1);
    tile(TP_VRAM, 0, 0, out);
    assert(memcmp(out, ref, sizeof ref) == 0);

    /* Refusals: netplay authority, an HD pack, an undefined bank. */
    sw_set_faithful_authority(1);
    assert(!gr_texture_banks_supported() && !gr_select_texture_bank(1, 1));
    sw_set_faithful_authority(0);
    hd_active = 1;
    assert(!gr_texture_banks_supported() && !gr_select_texture_bank(1, 1));
    hd_active = 0;
    assert(!gr_select_texture_bank(7, 0));
    assert(gr_texture_banks_supported());

    printf("sw_texture_banks_test: all checks passed\n");
    return 0;
}
