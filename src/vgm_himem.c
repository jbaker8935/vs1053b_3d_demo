/*
 * vgm_himem.c -- VGM high-memory cache backend for F256 Jr.
 *
 * Caches the entire VGM file into an extended 512 KiB RAM block using
 * movedown24
 *
 * After vgm_himem_load() the SD card is no longer accessed during playback.
 */

#include "f256lib.h"
#include "../include/oscar64_compat.h"
#include "../include/vgm_himem.h"

#define VGM_HIMEM_MAX_BYTES 524288UL

/* Read little-endian 32-bit value from a buffer. */
static uint32_t vgm_himem_read_le32(const uint8_t *hdr, uint8_t off)
{
    return (uint32_t)hdr[off]
         | ((uint32_t)hdr[off + 1u] << 8u)
         | ((uint32_t)hdr[off + 2u] << 16u)
         | ((uint32_t)hdr[off + 3u] << 24u);
}

/* Check whether a VGM file is compatible with the YMF262 (OPL3) player.
 * Returns true if the file is compatible; false if it contains known
 * unsupported chip targets such as the SN76489 PSG or YM2413 (OPLL).
 */
static bool vgm_himem_is_compatible_with_opl3(const uint8_t *hdr, uint16_t len)
{
    /* Need at least enough bytes to cover the clock fields. */
    if (len < 0x14u) {
        return true;
    }

    /* VGM header fields (LE32):
     *   0x0C = SN76489 clock
     *   0x10 = YM2413 clock
     * If these are nonzero, the VGM likely targets those chips.
     */
    uint32_t sn76489_clk = vgm_himem_read_le32(hdr, 0x0Cu);
    if (sn76489_clk != 0u) {
        textPrint("VGM file targets SN76489 PSG, which is not supported by the YMF262 OPL3.\n");
        return false;
    }

    uint32_t ym2413_clk = vgm_himem_read_le32(hdr, 0x10u);
    if (ym2413_clk != 0u) {
        textPrint("VGM file targets YM2413 (OPLL), which is not supported by the YMF262 OPL3.\n");
        return false;
    }

    return true;
}

/* -----------------------------------------------------------------------
 * movedown24 -- cross-bank descending block move using the 65816 MVN instruction.
 * for overlapping regions where the src is higher than the dst
 *
 * void movedown24(uint32_t dest, uint32_t src, uint16_t count)
 *
 * Based on http://6502org.wikidot.com/software-65816-memorymove
 * 
 * Handles copies that span 64 KiB bank boundaries by splitting into segments.
 * Each MVN call moves min($FFFF-X, $FFFF-Y, count) bytes so that neither the
 * src (X) nor dst (Y) 16-bit pointer wraps past $FFFF in a single call.
 * After each call, if X==0 the src bank is incremented; if Y==0 the dst bank
 * is incremented.  Loop until count reaches zero.
 *
 * llvm-mos calling convention (uint32_t, uint32_t, uint16_t):
 *   dest lo:hi:bank  -> A : X : __rc2
 *   src  lo:hi:bank  -> __rc4 : __rc5 : __rc6
 *   count lo:hi      -> __rc8 : __rc9
 *
 * ZP scratch (all __rcN caller-saved in llvm-mos):
 *   __rc6:__rc7  dst 16-bit addr (rebuilt from A:X on entry)
 *   __rc10       src bank
 *   __rc11       dst bank
 *   __rc12:__rc13 chunk size - 1 (saved before MVN; used to update count)
 * ----------------------------------------------------------------------- */

#ifndef __OSCAR64__

void movedown24(uint32_t dest, uint32_t src, uint16_t count);
asm(
    ".text\n"
    ".global movedown24\n"
    "movedown24:\n"

    /* Save src bank before clobbering __rc6. */
    "pha\n"                    /* push dst_lo                           */
    "lda __rc6\n"              /* src bank                              */
    "sta __rc10\n"
    "lda __rc2\n"              /* dst bank                              */
    "sta __rc11\n"
    "pla\n"                    /* restore dst_lo                        */
    "sta __rc6\n"              /* __rc6 = dst_lo                        */
    "stx __rc7\n"              /* __rc7 = dst_hi  => __rc6:__rc7=dst16  */

    /* Patch self-modifying MVN operand bytes while still in 8-bit mode */
    "lda __rc11\n"
    "sta __mdn24_dst\n"
    "lda __rc10\n"
    "sta __mdn24_src\n"

    /* Enter critical section */
    "php\n"
    "sei\n"
    "lda $00\n"
    "ora #$08\n"
    "sta $00\n"
    "lda $01\n"
    "pha\n"            /* save $01                              */
    "ora #$30\n"       /* bits4+5: Moves IO and Cart to Hi-Mem  */
    "sta $01\n"

    /* Switch to 65816 native 16-bit mode */
    "clc\n"
    ".byte $fb\n"              /* XCE                                   */
    ".byte $c2, $30\n"         /* REP #$30 -- 16-bit A, X, Y            */

    "ldx __rc4\n"              /* X = src 16-bit addr                   */
    "ldy __rc6\n"              /* Y = dst 16-bit addr                   */

    "__mdn24_loop:\n"

    "txa\n"
    ".byte $49, $ff, $ff\n"    /* EOR #$FFFF = $FFFF-X                  */
    "sta __rc12\n"

    "tya\n"
    ".byte $49, $ff, $ff\n"    /* EOR #$FFFF = $FFFF-Y                  */
    "cmp __rc12\n"
    "bcc __mdn24_have_min\n"
    "lda __rc12\n"
    "__mdn24_have_min:\n"

    "cmp __rc8\n"
    "bcc __mdn24_do_mvn\n"
    "lda __rc8\n"
    ".byte $3a\n"

    "__mdn24_do_mvn:\n"
    "sta __rc12\n"

    ".byte $54\n"              /* MVN dest_bank, src_bank               */
    "__mdn24_dst:\n"
    ".byte $00\n"
    "__mdn24_src:\n"
    ".byte $00\n"
    ".byte $4b\n"              /* PHK -- push PBR (= 0) onto stack      */
    ".byte $ab\n"              /* PLB -- pull stack top -> DBR = 0      */

    "lda __rc8\n"
    ".byte $3a\n"
    "sec\n"
    "sbc __rc12\n"
    "sta __rc8\n"

    "beq __mdn24_done\n"

    ".byte $e0, $00, $00\n"    /* CPX #0                                */
    "bne __mdn24_check_dst\n"
    ".byte $e2, $20\n"         /* SEP #$20 -> 8-bit A                   */
    "inc __mdn24_src\n"
    ".byte $c2, $20\n"         /* REP #$20 -> 16-bit A                  */

    "__mdn24_check_dst:\n"
    ".byte $c0, $00, $00\n"    /* CPY #0                                */
    "bne __mdn24_loop\n"
    ".byte $e2, $20\n"         /* SEP #$20 -> 8-bit A                   */
    "inc __mdn24_dst\n"
    ".byte $c2, $20\n"         /* REP #$20 -> 16-bit A                  */
    "jmp __mdn24_loop\n"

    "__mdn24_done:\n"

    "sec\n"
    ".byte $fb\n"              /* XCE                                   */

    "lda $00\n"
    "and #$F7\n"
    "sta $00\n"
    "pla\n"
    "sta $01\n"
    "plp\n"
    "rts\n"
);

#else /* __OSCAR64__ */
void movedown24(uint32_t dest, uint32_t src, uint16_t count) {
/*
 * llvm-mos calling convention (uint32_t, uint32_t, uint16_t):
 *   dest lo:hi:bank  -> A : X : __rc2
 *   src  lo:hi:bank  -> __rc4 : __rc5 : __rc6
 *   count lo:hi      -> __rc8 : __rc9
 *
 * ZP scratch (all __rcN caller-saved in llvm-mos):
 *   __rc6:__rc7  dst 16-bit addr (rebuilt from A:X on entry)
 *   __rc10       src bank
 *   __rc11       dst bank
 *   __rc12:__rc13 chunk size - 1 (saved before MVN; used to update count)
 */
     uint16_t chunk_size=0;
 __asm {

    /* Patch self-modifying MVN operand bytes while still in 8-bit mode */
    lda dest+2
    sta __mdn24_dst
    lda src+2
    sta __mdn24_src

    /* Enter critical section */
    php
    sei
    lda 0x00
    ora #0x08
    sta 0x00
    lda 0x01
    pha            /* save 0x01                              */
    ora #0x30       /* bits4+5: Moves IO and Cart to Hi-Mem  */
    sta 0x01

    /* Switch to 65816 native 16-bit mode */
    clc
    byt 0xfb              /* XCE                                   */
    byt 0xc2              /* REP #0x30 -- 16-bit A, X, Y            */
    byt 0x30
    ldx src              /* X = src 16-bit addr                   */
    ldy dest              /* Y = dst 16-bit addr                   */

    __mdn24_loop:

    txa
    byt 0x49    /* EOR #0xFFFF = 0xFFFF-X                  */
    byt 0xff
    byt 0xff
    sta chunk_size

    tya
    byt 0x49    /* EOR #0xFFFF = 0xFFFF-Y                  */
    byt 0xff
    byt 0xff
    
    cmp chunk_size
    bcc __mdn24_have_min
    lda chunk_size
    __mdn24_have_min:

    cmp count
    bcc __mdn24_do_mvn
    lda count
    byt 0x3a

    __mdn24_do_mvn:
    sta chunk_size

    byt 0x54              /* MVN dest_bank, src_bank               */
    __mdn24_dst:
    byt 0x00
    __mdn24_src:
    byt 0x00
    byt 0x4b              /* PHK -- push PBR (= 0) onto stack      */
    byt 0xab              /* PLB -- pull stack top -> DBR = 0      */

    lda count
    byt 0x3a
    sec
    sbc chunk_size
    sta count

    beq __mdn24_done

    byt 0xe0     /* CPX #0                                */
    byt 0x00
    byt 0x00
    bne __mdn24_check_dst
    byt 0xe2         /* SEP #0x20 -> 8-bit A                   */
    byt 0x20
    inc __mdn24_src
    byt 0xc2          /* REP #0x20 -> 16-bit A                  */
    byt 0x20

    __mdn24_check_dst:
    byt 0xc0    /* CPY #0                                */
    byt 0x00
    byt 0x00
    bne __mdn24_loop
    byt 0xe2         /* SEP #0x20 -> 8-bit A                   */
    byt 0x20
    inc __mdn24_dst
    byt 0xc2         /* REP #0x20 -> 16-bit A                  */
    byt 0x20
    jmp __mdn24_loop

    __mdn24_done:

    sec
    byt 0xfb              /* XCE                                   */

    lda 0x00
    and #0xF7
    sta 0x00
    pla
    sta 0x01
    plp
    rts
}
}
#endif /* __OSCAR64__ */


/* -----------------------------------------------------------------------
 * vgm_himem_read -- vgm_read_fn callback.
 *
 * Copies `len` bytes from the high-memory cache into the player's near-RAM
 * buffer using movedown24 
 * ----------------------------------------------------------------------- */
uint16_t vgm_himem_read(void *ctx, uint8_t *buf, uint16_t len)
{
    vgm_himem_ctx_t *hm = (vgm_himem_ctx_t *)ctx;
    if (hm->pos >= hm->size) {
        return 0u;
    }
    uint32_t avail = hm->size - hm->pos;
    if ((uint32_t)len > avail) {
        len = (uint16_t)avail;
    }
    movedown24((uint32_t)(uintptr_t)buf, hm->base + hm->pos, len);
    hm->pos += (uint32_t)len;
    return len;
}

/* -----------------------------------------------------------------------
 * vgm_himem_seek -- vgm_seek_fn callback.
 *
 * Updates the stream position in the context.  No I/O -- the entire file
 * is already in high memory.
 * ----------------------------------------------------------------------- */
void vgm_himem_seek(void *ctx, uint32_t offset)
{
    ((vgm_himem_ctx_t *)ctx)->pos = offset;
}

/* -----------------------------------------------------------------------
 * kernelReadC 
 * ----------------------------------------------------------------------- */
#ifndef __OSCAR64__
#pragma push_macro("EOF")
#endif
#undef EOF
static __attribute__((noinline)) int16_t kernelReadC(uint8_t fd, void *buf, uint16_t nbytes)
{
    KARGS(file.read.stream) = fd;
    KARGS(file.read.buflen) = nbytes;
    kernelCall(File.Read);
    if (kernelError) return -1;

    for (;;) {
        kernelNextEvent();
        size_t ev = kernelEventData.type;
        if (ev == kernelEvent(file.DATA)) {
                KARGS(common.buf) = buf;
                KARGS(common.buflen) = KEVENT_FILE_DATA(data.delivered);
                kernelCall(ReadData);
                return (int16_t)KEVENT_FILE_DATA(data.delivered);
        } else if (ev == kernelEvent(file.EOF)) {
                return 0;
        } else if (ev == kernelEvent(file.ERROR)) {
                return -1;
        }
    }
}
#ifndef __OSCAR64__
#pragma pop_macro("EOF")
#else
#define EOF (-1)
#endif

/* -----------------------------------------------------------------------
 * vgm_himem_load -- read a VGM file into high memory.
 *
 * Opens the file with fileOpen, then loops calling kernelReadC (255 bytes
 * at a time) until EOF.  Each chunk is copied to high memory by movedown24.
 * ----------------------------------------------------------------------- */
#ifndef __OSCAR64__
__attribute__((noinline))
#endif
bool vgm_himem_load(const char *path, uint32_t base_addr, vgm_himem_ctx_t *ctx)
{
    static uint8_t s_chunk[255];

    uint8_t *fd = fileOpen((char *)path, "r");
    if (!fd) {
        return false;
    }
    textGotoXY(0, 0);
    textPrint("Loading audio ...");
    uint32_t total = 0u;
    uint32_t chunks = 0u;
    int16_t  n;
    for (;;) {
        n = kernelReadC(*fd, s_chunk, 255);
        if (n <= 0) break;
        if (total == 0u) {
            if (n < 4 || s_chunk[0] != 'V' || s_chunk[1] != 'g' ||
                s_chunk[2] != 'm' || s_chunk[3] != ' ') {
                textGotoXY(0, 1);
                textPrint("Invalid VGM header.\n");
                fileClose(fd);
                return false;
            }
            if (!vgm_himem_is_compatible_with_opl3(s_chunk, (uint16_t)n)) {
                fileClose(fd);
                return false;
            }
        }
        if (total + (uint32_t)(uint16_t)n > VGM_HIMEM_MAX_BYTES) {
            textGotoXY(0, 1);
            textPrint("VGM file exceeds 512K.\n");
            fileClose(fd);
            return false;
        }
        movedown24(base_addr + total,
                (uint32_t)(uintptr_t)s_chunk,
                (uint16_t)n);
        total += (uint32_t)(uint16_t)n;
        chunks += 1u;
        if ((chunks % 200u) == 0u) {
            textPrint(".");
        }
    }
    textGotoXY(0, 1);
    fileClose(fd);
    ctx->base = base_addr;
    ctx->size = total;
    ctx->pos  = 0u;
    return (total > 0u);
}

