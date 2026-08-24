/* psx_cheats.h -- GameShark/PEC-style runtime cheat engine.
 *
 * Classic PSX cheat codes are per-frame RAM pokes, not image patches: the
 * device rewrites the target words every VBlank so the game cannot undo them.
 * Mod-package `main_exe` patches cannot express that -- they only touch the
 * executable image before boot, and most published codes target BSS/heap
 * addresses that do not exist in the image at all.
 *
 * Codes are read from a plain text file (see psx_cheats_load) and applied on
 * guest VBlank. Writes are restricted to main RAM.
 */
#ifndef PSX_CHEATS_H
#define PSX_CHEATS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Load (or reload) codes. Passing NULL re-reads the last path. Safe to call
 * when the file is absent: the engine simply stays inactive. */
void psx_cheats_load(const char *path);

/* Apply every enabled code once. Call from the guest VBlank handler. */
void psx_cheats_on_vblank(void);

/* Number of enabled cheats (0 when the engine is inactive). */
int psx_cheats_enabled_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PSX_CHEATS_H */
