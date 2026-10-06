#ifndef POKEVAULT_CRYPTO_H
#define POKEVAULT_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint16_t pv_read16(const uint8_t *p);
uint32_t pv_read32(const uint8_t *p);
void pv_write16(uint8_t *p, uint16_t v);
void pv_write32(uint8_t *p, uint32_t v);

uint16_t pv_add16(const uint8_t *p, size_t len);
uint16_t pv_crc16_ccitt(const uint8_t *p, size_t len);
uint16_t pv_checksum32(const uint8_t *p, size_t len);

/* Returns true when the Pokémon record checksum matches after decryption. */
bool pv_decrypt45(uint8_t *data, int len);
bool pv_decrypt3(uint8_t *data);

uint8_t pv_level_from_exp(uint32_t exp, uint8_t growth);
bool pv_is_shiny(uint32_t pid, uint16_t tid, uint16_t sid);

#ifdef HOST_TEST
void pv_refresh_checksum45(uint8_t *data);
void pv_refresh_checksum3(uint8_t *data);
void pv_encrypt45(uint8_t *data, int len);
void pv_encrypt3(uint8_t *data);
#endif

#endif
