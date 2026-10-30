/*
 * iso_reader_c.cpp — C wrapper for the C++ ISOReader class
 *
 * Provides iso_open() / iso_read_sector() / iso_close() for use by cdrom.c
 */

#include "iso_reader.h"
#include "mod_runtime.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

/* True when lba lies past the stock image but inside sectors an active mod
 * appends. Such sectors start zeroed and the mod layer fills them; with no
 * mod providing them the read fails exactly as before. */
static bool mod_appended_sector(PS1::ISOReader* reader, uint32_t lba) {
    return lba >= reader->GetSectorCount() &&
           lba < mod_runtime_disc_sector_count();
}

extern "C" {

void* iso_open(const char* path) {
    auto* reader = new PS1::ISOReader();
    if (!reader->Open(path)) {
        delete reader;
        return nullptr;
    }
    return reader;
}

int iso_read_sector(void* handle, uint32_t lba, uint8_t* buffer, int size) {
    if (!handle) return 0;
    auto* reader = static_cast<PS1::ISOReader*>(handle);
    (void)size; /* ReadSector always reads 2048 bytes */
    if (!reader->ReadSector(lba, buffer)) {
        if (!mod_appended_sector(reader, lba)) return 0;
        /* An appended sector exists only as what the mods provide. Build its
         * raw form so raw claims (and the Form1 user claims they cascade to)
         * land, then serve the user window; a sector no raw claim gave a
         * Mode2 Form1 header takes the user claims directly. */
        uint8_t raw[2352] = {};
        mod_runtime_patch_disc_sector(lba, 1, raw, sizeof(raw));
        if (raw[15] == 2 && (raw[18] & 0x20u) == 0) {
            std::memcpy(buffer, raw + 24, 2048);
            return 1;
        }
        std::memset(buffer, 0, 2048);
    }
    mod_runtime_patch_disc_sector(lba, 0, buffer, 2048);
    return 1;
}

int iso_read_raw_sector(void* handle, uint32_t lba, uint8_t* buffer, int size) {
    if (!handle || size < 2352) return 0;
    auto* reader = static_cast<PS1::ISOReader*>(handle);
    if (!reader->ReadRawSector(lba, buffer)) {
        if (!mod_appended_sector(reader, lba)) return 0;
        std::memset(buffer, 0, 2352);
    }
    mod_runtime_patch_disc_sector(lba, 1, buffer, 2352);
    return 1;
}

int iso_read_subq(void* handle, uint32_t lba, uint8_t* buffer, int size,
                  int* valid) {
    if (!handle || !buffer || size < 12 || !valid) return 0;
    bool crc_valid = false;
    if (!static_cast<PS1::ISOReader*>(handle)->ReadSubChannelQ(
            lba, buffer, &crc_valid)) return 0;
    *valid = crc_valid ? 1 : 0;
    return 1;
}

int iso_has_subq_replacements(void* handle) {
    return handle && static_cast<PS1::ISOReader*>(handle)->HasSubChannelReplacements();
}

uint32_t iso_sector_count(void* handle) {
    if (!handle) return 0;
    auto* reader = static_cast<PS1::ISOReader*>(handle);
    return std::max(reader->GetSectorCount(), mod_runtime_disc_sector_count());
}

/* CD-track TOC accessors (multi-track / CD-DA support). track is 1-based. */
int iso_track_count(void* handle) {
    if (!handle) return 1;
    return static_cast<PS1::ISOReader*>(handle)->TrackCount();
}

uint32_t iso_track_start_lba(void* handle, int track) {
    if (!handle) return 0;
    return static_cast<PS1::ISOReader*>(handle)->TrackStartLBA(track);
}

uint32_t iso_track_pregap_lba(void* handle, int track) {
    auto* reader = static_cast<PS1::ISOReader*>(handle);
    return reader ? reader->TrackPregapLBA(track) : 0;
}

int iso_track_is_audio(void* handle, int track) {
    if (!handle) return 0;
    return static_cast<PS1::ISOReader*>(handle)->TrackIsAudio(track) ? 1 : 0;
}

void iso_close(void* handle) {
    if (!handle) return;
    auto* reader = static_cast<PS1::ISOReader*>(handle);
    reader->Close();
    delete reader;
}

} /* extern "C" */
