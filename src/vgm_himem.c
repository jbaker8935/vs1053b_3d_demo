/*
 * vgm_himem.c -- VGM high-memory cache backend for F256 Jr.
 *
 * Caches the entire VGM file into an extended 512 KiB RAM block using
 * movedown24 (65816 MVN via the core2x flat-memory extension) for bulk copies,
 * and POKE24/PEEK24 (W65C02S STA [zp] / LDA [zp]) for single-byte fallback.
 *
 * After vgm_himem_load() the SD card is no longer accessed during playback.
 */

#include "f256lib.h"
#include "../include/vgm_himem.h"

#define DEBUG_FIRST_CHUNK 0
#define VGM_HIMEM_MAX_BYTES 524288UL

static void vgm_himem_pause_for_message(void)
{
    for (volatile uint32_t i = 0; i < 200000ul; i++) {
        __asm__ volatile("nop");
    }
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


/* -----------------------------------------------------------------------
 * vgm_himem_read -- vgm_read_fn callback.
 *
 * Copies `len` bytes from the high-memory cache into the player's near-RAM
 * buffer using movedown24 (~4 cycles/byte vs ~29 for SPI).
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
 * kernelReadC -- single-shot kernel file read (no internal retry loop).
 *
 * Unlike fileRead / kernelRead, this issues one File.Read request and
 * returns immediately with however many bytes the kernel delivered.
 * Returns 0 on EOF, -1 on error.  The caller loops until 0 or -1.
 *
 * The EOF macro from f256lib conflicts with kernelEvent(file.EOF), so we
 * temporarily suppress it with push/pop_macro.
 * ----------------------------------------------------------------------- */
#pragma push_macro("EOF")
#undef EOF
/* noinline: prevents the compiler from merging this function's body into
 * vgm_himem_load.  When inlined, the optimizer sees the kernelCall leaf-asm
 * clobbers ("a","c","v" only) and incorrectly keeps total.lo in the Y
 * register across the kernel JSRs.  As a separate call the standard 6502 ABI
 * applies, which treats A/X/Y as caller-saved and forces a proper spill. */
static __attribute__((noinline)) int16_t kernelReadC(uint8_t fd, void *buf, uint16_t nbytes)
{
    kernelArgs->file.read.stream = fd;
    kernelArgs->file.read.buflen = nbytes;
    kernelCall(File.Read);
    if (kernelError) return -1;

    for (;;) {
        kernelNextEvent();
        switch (kernelEventData.type) {
            case kernelEvent(file.DATA):
                kernelArgs->common.buf = buf;
                kernelArgs->common.buflen = kernelEventData.file.data.delivered;
                kernelCall(ReadData);
                return (int16_t)kernelEventData.file.data.delivered;
            case kernelEvent(file.EOF):
                return 0;
            case kernelEvent(file.ERROR):
                return -1;
            default:
                continue;
        }
    }
}
#pragma pop_macro("EOF")

/* -----------------------------------------------------------------------
 * vgm_himem_load -- read a VGM file into high memory.
 *
 * Opens the file with fileOpen, then loops calling kernelReadC (255 bytes
 * at a time) until EOF.  Each chunk is copied to high memory by movedown24.
 * The static staging buffer avoids a 255-byte local on the 6502 stack.
 * ----------------------------------------------------------------------- */
/* noinline: this function is LTO-inlined into main() without this attribute.
 * Combined with the large main() frame, the register allocator keeps the
 * loop variable total.lo in Y across kernelReadC() calls, and if the kernel
 * modifies Y (which the kernelCall clobber list does not declare), the
 * destination address for each movedown24() call is corrupted. */
__attribute__((noinline))
bool vgm_himem_load(const char *path, uint32_t base_addr, vgm_himem_ctx_t *ctx)
{
    /* 255 bytes: maximum for kernelReadC (buflen is uint8_t). */
    static uint8_t s_chunk[255];

    uint8_t *fd = fileOpen((char *)path, "r");
    if (!fd) {
        textPrint("\nfopen FAILED");
        return false;
    }

    textPrint("\nLoading audio ...");

    uint32_t total = 0u;
    uint32_t chunks = 0u;
    int16_t  n;
    for (;;) {
        n = kernelReadC(*fd, s_chunk, 255u);
        if (n <= 0) break;
        if (total == 0u) {
            if (n < 4 || s_chunk[0] != 'V' || s_chunk[1] != 'g' ||
                s_chunk[2] != 'm' || s_chunk[3] != ' ') {
                textPrint("\nInvalid VGM header.  No audio will be played.\n");
                fileClose(fd);
                vgm_himem_pause_for_message();
                return false;
            }
        }
#if DEBUG_FIRST_CHUNK
        if (total == 0u) {
            /* Debug: verify the first chunk read from disk is real file data */
            char dbg[64];
            snprintf(dbg, sizeof(dbg), "\nvgm load read: %02x%02x%02x%02x (n=%u)",
                     s_chunk[0], s_chunk[1], s_chunk[2], s_chunk[3], (unsigned)n);
            textPrint(dbg);
        }
#endif
        if (total + (uint32_t)(uint16_t)n > VGM_HIMEM_MAX_BYTES) {
            textPrint("\nVGM file exceeds 512K.  No audio will be played.\n");
            fileClose(fd);
            vgm_himem_pause_for_message();
            return false;
        }
        movedown24(base_addr + total,
                (uint32_t)(uintptr_t)s_chunk,
                (uint16_t)n);
#if DEBUG_FIRST_CHUNK
        if (total == 0u) {
            /* Debug: read back the first bytes from hi-mem to confirm the copy */
            uint8_t verify[4];
            movedown24((uint32_t)(uintptr_t)verify, base_addr + total, 4u);
            char dbg[64];
            snprintf(dbg, sizeof(dbg), "\nvgm load write: %02x%02x%02x%02x\n",
                     verify[0], verify[1], verify[2], verify[3]);
            textPrint(dbg);
        }
#endif
        total += (uint32_t)(uint16_t)n;
        chunks += 1u;
        if ((chunks % 200u) == 0u) {
            textPrint(".");
        }
    }

    fileClose(fd);

    textPrint("\n");

    ctx->base = base_addr;
    ctx->size = total;
    ctx->pos  = 0u;
    return (total > 0u);
}
