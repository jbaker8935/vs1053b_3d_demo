#if !defined(INCLUDE_OPL3_IO_H__)
#define INCLUDE_OPL3_IO_H__

/*
 * opl3_io.h -- shared OPL3 (YMF262) hardware write primitives for the F256.
 *
 * Included by both vgm.c (the BGM player) and vgm_fx.c (the FX engine) so
 * both translation units emit identical OPL3 bus cycles without duplicating
 * the address definitions.
 *
 * Each write is a two-write sequence to memory-mapped I/O:
 *   1. Address register (OPL_ADDR_L for bank A, OPL_ADDR_H for bank B)
 *   2. Data register (shared)
 */

#include "f256lib.h"

/* F256 memory-mapped OPL3 registers */
#define OPL_ADDR_L  0xD580u  /* port-0 address register (regs 0x00-0xFF) */
#define OPL_DATA    0xD581u  /* shared data register                      */
#define OPL_ADDR_H  0xD582u  /* port-1 address register (regs 0x00-0xFF) */

/* Write one OPL3 register on port 0 (bank A, channels 0-8). */
static inline void opl_write_port0(uint8_t reg, uint8_t val)
{
    POKE(OPL_ADDR_L, reg);
    POKE(OPL_DATA, val);
}

/* Write one OPL3 register on port 1 (bank B, channels 9-17). */
static inline void opl_write_port1(uint8_t reg, uint8_t val)
{
    POKE(OPL_ADDR_H, reg);
    POKE(OPL_DATA, val);
}

#endif /* INCLUDE_OPL3_IO_H__ */
