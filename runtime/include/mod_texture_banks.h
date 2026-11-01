#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
const uint16_t* mod_texture_bank_pixels(uint16_t id, uint32_t* width, uint32_t* height);
/* 1 when the bank (resolved if missing) was defined with a live palette. */
int mod_texture_bank_live_clut(uint16_t id);
/* Game uploads a mod declared inside the bank, as {x, y, width_words, height}. */
uint32_t mod_texture_bank_uploads(uint16_t id, const uint16_t (**uploads)[4]);
uint16_t mod_texture_packet_bank(uint32_t source, const uint32_t* words, uint32_t count);
int mod_texture_packet_precision(uint32_t source, float q[3], float xy[6]);
int mod_texture_bank_batchable(int immutable, int mask_check, int semi);
#ifdef __cplusplus
}
#endif
