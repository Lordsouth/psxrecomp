/* Mod card subfolder: the name check and the active selection, split from
 * memcard.c so the mod package catalog (mod_packages.cpp, which validates
 * psx_mod_register_memcard_subdir names and applies them on activation)
 * links without the card hardware model. */
#include "memcard.h"
#include <stdio.h>
#include <string.h>

/* Card subfolder requested by a mod activation callback (mod_plugins.h), for
 * a mod whose saves must not mix with the stock game's. main.cpp clears it
 * before every activation pass, so a session with the mod off starts clean,
 * and it survives a rematch that re-inits cards without re-activating. */
static char s_mod_subdir[64];

void memcard_clear_mod_subdir(void) { s_mod_subdir[0] = '\0'; }

const char* memcard_mod_subdir(void) { return s_mod_subdir; }

int memcard_subdir_name_valid(const char* name) {
    size_t i;
    if (!name || !name[0] || strlen(name) >= sizeof(s_mod_subdir)) return 0;
    for (i = 0; name[i]; i++) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

int psx_mod_set_memcard_subdir(const char* name) {
    if (!memcard_subdir_name_valid(name)) return 0;
    snprintf(s_mod_subdir, sizeof(s_mod_subdir), "%s", name);
    return 1;
}
