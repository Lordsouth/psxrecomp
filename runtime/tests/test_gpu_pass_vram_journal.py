"""A plugin VRAM write inside a render pass is journaled before its pixels land.

The pass journal (gpu_gl_renderer.c pass_refuse_write) snapshots CPU VRAM so
the pass can roll the write back. gpu_mod_vram_write must open that journal
before it copies into vram[], or the snapshot holds the new pixels and the
write leaks out of the pass.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', default='gcc')
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'src/gpu.c')
a = p.parse_args()
s = a.source.read_text()
start = s.index('int gpu_mod_vram_write(')
end = s.index('\n}\n', start) + 3
production = s[start:end]
harness = r"""
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
static uint16_t vram[1024 * 512];
static int g_psx_render_pass_active, notes;
static uint16_t journaled;
static void gl_renderer_pass_note_upload_open(int x, int y, int w, int h) {
    assert(x == 8 && y == 4 && w == 2 && h == 1);
    journaled = vram[4 * 1024 + 8];   /* what the rollback would restore */
    notes++;
}
static void gpu_vram_dirty_mark_rect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void gr_vram_transfer_in(int x, int y, int w, int h, const uint16_t *p) {
    (void)x; (void)y; (void)w; (void)h; (void)p;
}
int gpu_mod_vram_write(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t* pixels);
""" + production + r"""
int main(void) {
    const uint16_t px[2] = {0x7FFF, 0x1234};
    vram[4 * 1024 + 8] = 0x0042;
    assert(gpu_mod_vram_write(8, 4, 2, 1, px) && notes == 0);   /* outside a pass: no journal */
    vram[4 * 1024 + 8] = 0x0042;
    g_psx_render_pass_active = 1;
    assert(gpu_mod_vram_write(8, 4, 2, 1, px) && notes == 1);
    assert(journaled == 0x0042 && vram[4 * 1024 + 8] == 0x7FFF);
    puts("PASS plugin VRAM write is journaled before it lands");
    return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
    root = Path(d)
    c = root / 'journal.c'
    c.write_text(harness)
    exe = root / 'journal.exe'
    subprocess.run([a.cc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
