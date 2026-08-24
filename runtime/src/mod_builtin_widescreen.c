/*
 * Framework-owned widescreen activation plugin.
 *
 * Widescreen is deliberately mod-owned on PSX: main.cpp pins ws_offered false
 * and clamps the generic [video] aspect_ratio to 4:3 during launcher/settings
 * resolution, so neither a stale settings value nor a legacy game.toml can
 * engage a wider field of view behind the player's back. Trusted activation
 * plugins run after that resolution -- this is the one that turns it on.
 *
 * What this buys is the GTE X-squash tier: the projection is widened so 3D
 * scenes show more world horizontally, and both renderers letterbox to the
 * chosen aspect. That tier is generic, which is why it ships here against
 * game_id "*".
 *
 * What it does NOT do is the per-title work: tagging the functions that build
 * character/billboard sprites so they are not stretched, proving the cull
 * sites that decide what is drawn at the revealed edges, and squashing 2D
 * parallax backdrops. Those need per-game reverse engineering and live in the
 * game's own [widescreen] block (psxrecomp/WIDESCREEN.md). A title that has
 * not done them still gets a wider view; it may also get stretched sprites and
 * pop-in at the new edges, which is why this defaults to off.
 */
#include "mod_plugins.h"

#include <string.h>

#define PKG_WS "psx.enhancement.widescreen"

/* main.cpp -- applies the aspect and re-projects, after the 4:3 clamp. */
extern void psx_ws_engage_aspect(int num, int den);

static void builtin_widescreen_activate(void) {
    char text[16] = "";
    int num = 16, den = 9;
    if (psx_mod_option_value(PKG_WS, "widescreen", "aspect",
                             text, sizeof text)) {
        if      (strcmp(text, "16:10") == 0) { num = 16; den = 10; }
        else if (strcmp(text, "21:9")  == 0) { num = 21; den = 9;  }
        else if (strcmp(text, "4:3")   == 0) { num = 4;  den = 3;  }
    }
    psx_ws_engage_aspect(num, den);
}

PSX_MOD_CONSTRUCTOR(psx_register_builtin_widescreen_plugin) {
    (void)psx_mod_register_activation_plugin("psx.widescreen",
                                             builtin_widescreen_activate);
}
