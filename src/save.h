#ifndef SF_SAVE_H
#define SF_SAVE_H

#include <stddef.h>
#include <stdint.h>

/* Outside the updater's work folder (SAVE_DIR "/update"), which the updater
 * empties on its own; nothing here is ever touched by it. */
#define SAVE_DIR  "ux0:data/Spanfall"
#define SAVE_PATH "ux0:data/Spanfall/best.dat"

/* 12 bytes, little endian: "SPNF", best score (uint32), FNV-1a of bytes 0..7. */
#define SAVE_SIZE 12

typedef enum { SAVE_OK = 0, SAVE_MISSING, SAVE_CORRUPT } SaveStatus;

void save_encode(uint32_t best, uint8_t out[SAVE_SIZE]);
/* 0 and *best set if the blob is a valid save; -1 otherwise (*best untouched). */
int save_decode(const uint8_t *in, size_t n, uint32_t *best);

/* Reads path, then path.tmp, then path.bak, and takes the first valid copy.
 * *best is 0 unless SAVE_OK. SAVE_MISSING means no copy exists at all. */
SaveStatus save_load(const char *path, uint32_t *best);

/* Writes path.tmp, checks it reads back, then rotates path -> path.bak and
 * path.tmp -> path (the Vita's rename refuses an existing target). At every
 * step one valid copy is on disk. The folder must already exist. 0 ok. */
int save_write(const char *path, uint32_t best);

#endif
