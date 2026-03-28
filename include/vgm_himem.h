#ifndef VGM_HIMEM_H
#define VGM_HIMEM_H

#include <stdint.h>
#include <stdbool.h>

/*
 * vgm_himem.h -- VGM high-memory cache backend for F256 Jr.
 *
 * Before the animation loop starts, call vgm_himem_load() to read the VGM
 * file into one of the F256 Jr.'s extended 512 KiB RAM blocks.  Then pass
 * vgm_himem_read / vgm_himem_seek / &ctx as the callbacks to vgm_open().
 *
 * High-memory access uses movedown24 (65816 MVN via the core2x flat-memory
 * extension).  Setting bit 3 of $00 (MMU control) disables the MMU and
 * enables direct flat physical addressing for the duration of the MVN.
 *
 * Extended RAM blocks on F256 Jr.:
 *   0x080000UL  -- first 512 KiB block
 *   0x100000UL  -- second 512 KiB block
 *   0x180000UL  -- third 512 KiB block
 */

/* Stream context for the high-memory VGM backend. */
typedef struct {
    uint32_t base;   /* 24-bit physical start of the cached VGM data       */
    uint32_t size;   /* total bytes written by vgm_himem_load()             */
    uint32_t pos;    /* current read position; reset to 0 before vgm_open()*/
} vgm_himem_ctx_t;

/*
 * vgm_himem_load -- cache a VGM file in high memory.
 *
 *   path      : POSIX path on the SD card, e.g. "media/vgm/music.vgm"
 *   base_addr : 24-bit physical destination, e.g. 0x080000UL
 *   ctx       : caller-allocated context; .base/.size/.pos are set on success
 *
 * Reads the file in chunks via kernelReadC and writes each byte into high
 * memory with POKE24 (W65C02S STA [zp] extended opcode).
 *
 * Returns true on success (at least one byte read), false if the file could
 * not be opened.
 *
 * __attribute__((noinline)) prevents LTO from merging this into its only
 * caller (main).  Without it, the combined frame's register allocator keeps
 * total.lo in Y register across kernel JSR calls whose clobber list omits
 * "y", causing corrupted himem destination addresses for all but the first
 * chunk.
 */
__attribute__((noinline))
bool vgm_himem_load(const char *path, uint32_t base_addr, vgm_himem_ctx_t *ctx);

/* vgm_read_fn-compatible callback: PEEK24 from high memory into buf. */
uint16_t vgm_himem_read(void *ctx, uint8_t *buf, uint16_t len);

/* vgm_seek_fn-compatible callback: update stream position (no I/O). */
void vgm_himem_seek(void *ctx, uint32_t offset);

/* Low-level 65816 MVN block copy; exposed for direct testing in main.c. */
void movedown24(uint32_t dest, uint32_t src, uint16_t count);


#endif /* VGM_HIMEM_H */
