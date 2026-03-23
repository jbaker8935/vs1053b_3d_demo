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
        return false;
    }
    textGotoXY(0, 0);
    textPrint("Loading audio ...");
    uint32_t total = 0u;
    uint32_t chunks = 0u;
    int16_t  n;
    for (;;) {
        n = kernelReadC(*fd, s_chunk, 255u);
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

/* -----------------------------------------------------------------------
 * vgm_himem_is_playable -- scan loaded VGM for timing complexity.
 *
 * Walks the VGM data body counting sub-quantum wait commands: any 0x70-0x7F
 * short-wait opcode (1-16 samples) or any 0x61 variable-wait with a count
 * below VGM_SHORT_WAIT_QUANTUM (220 samples, the 200 Hz grid).
 *
 * Files produced by Furnace tracker at 100 Hz use only 441-sample 0x61 waits
 * and pass with a count of zero.  Files with MIDI-accurate sub-sample timing
 * (e.g. OPL2/3 recordings) have hundreds of short waits and are rejected.
 *
 * If the file exceeds VGM_SHORT_WAIT_MAX, a diagnostic message is printed and
 * the function returns false.  The caller should then skip vgm_open() and let
 * the demo run silently.  Quantize offline with tools/vgm_quantize.py first.
 * ----------------------------------------------------------------------- */
#define VGM_SHORT_WAIT_QUANTUM  220u   /* 200 Hz in 44100 Hz samples       */
#define VGM_SHORT_WAIT_MAX      100u   /* short waits before rejection      */
/* Scan only the first 4 KiB of VGM body data.  Hangover-style files have
 * short waits every ~1 ms and will hit the threshold within the first KB.
 * Furnace 100 Hz files have zero short waits and pass immediately.
 * Scanning the full file on the 8 MHz 6502 takes 5-10 seconds for 300-460KB
 * files.  4 KB scans in < 100 ms. */
#define VGM_SCAN_MAX_BODY  4096u

/* Scan buffer -- shared BSS, not on the 256-byte 6502 stack. */
static uint8_t s_scan_buf[255u];

bool vgm_himem_is_playable(const vgm_himem_ctx_t *ctx)
{
    if (ctx->size < 0x40u) return true;

    /* Read header to find data_start. */
    uint16_t hlen = (ctx->size < 0x60u) ? (uint16_t)ctx->size : 0x60u;
    movedown24((uint32_t)(uintptr_t)s_scan_buf, ctx->base, hlen);

    uint32_t data_start;
    {
        uint16_t ver = (uint16_t)s_scan_buf[0x08u] | ((uint16_t)s_scan_buf[0x09u] << 8u);
        if (ver >= 0x150u && hlen >= 0x38u) {
            uint32_t raw = vgm_himem_read_le32(s_scan_buf, 0x34u);
            data_start = 0x34u + raw;
            if (data_start < 0x40u) data_start = 0x40u;
        } else {
            data_start = 0x40u;
        }
    }

    /* State machine states (stored in a uint8_t):
     *   0  SC_OP      : next byte is a command opcode
     *   1  SC_SKIPN   : skip skip_n more operand bytes, then back to SC_OP
     *   2  SC_W16LO   : 0x61 wait: next byte is sample-count low
     *   3  SC_W16HI   : 0x61 wait: next byte is sample-count high
     *   4  SC_DB0     : data block (0x67): type byte
     *   5  SC_DB1     : data block: compat byte
     *   6  SC_DB2..9  : data block: size LE32 bytes 0..3
     *  10  SC_DBSKIP  : skipping data block body (dblk bytes remain)
     */
    uint8_t  st     = 0u;
    uint8_t  skip_n = 0u;
    uint8_t  wlo    = 0u;
    uint32_t dblk   = 0u;
    uint16_t short_count = 0u;

    uint32_t pos  = data_start;
    uint32_t scan_end = data_start + VGM_SCAN_MAX_BODY;
    if (scan_end > ctx->size) scan_end = ctx->size;
    bool     done = false;

    while (!done && pos < scan_end) {

        /* Fast-skip data block body without byte-by-byte looping. */
        if (st == 10u && dblk > 0u) {
            uint32_t avail = scan_end - pos;
            uint32_t step  = (dblk < avail) ? dblk : avail;
            pos  += step;
            dblk -= step;
            if (dblk == 0u) st = 0u;
            continue;
        }

        uint16_t n = (uint16_t)(scan_end - pos);
        if (n > 255u) n = 255u;
        movedown24((uint32_t)(uintptr_t)s_scan_buf, ctx->base + pos, n);
        pos += (uint32_t)n;

        for (uint16_t i = 0u; i < n && !done; ++i) {
            uint8_t b = s_scan_buf[i];
            switch (st) {
            case 0u: /* SC_OP */
                if (b >= 0x70u && b <= 0x7Fu) {
                    /* Short wait: 1-16 samples */
                    ++short_count;
                } else if (b == 0x61u) {
                    st = 2u; /* SC_W16LO */
                } else if (b == 0x66u) {
                    done = true;
                } else if (b == 0x67u) {
                    st = 4u; dblk = 0u; /* SC_DB0 */
                } else if (b == 0x62u || b == 0x63u ||
                           (b >= 0x80u && b <= 0x8Fu)) {
                    /* 1-byte opcodes, no operands */
                } else if ((b >= 0x30u && b <= 0x3Fu) ||
                           b == 0x4Fu || b == 0x50u) {
                    skip_n = 1u; st = 1u;
                } else if ((b >= 0x40u && b <= 0x4Eu) ||
                           (b >= 0x51u && b <= 0x5Fu) ||
                           b == 0xA0u ||
                           (b >= 0xB0u && b <= 0xC8u)) {
                    skip_n = 2u; st = 1u;
                } else if (b >= 0xC9u && b <= 0xDFu) {
                    skip_n = 3u; st = 1u;
                } else if (b >= 0xE0u) {
                    skip_n = 4u; st = 1u;
                }
                /* unknown single-byte: stay SC_OP */
                break;
            case 1u: /* SC_SKIPN */
                if (--skip_n == 0u) st = 0u;
                break;
            case 2u: /* SC_W16LO */
                wlo = b; st = 3u;
                break;
            case 3u: /* SC_W16HI */ {
                uint16_t v = (uint16_t)wlo | ((uint16_t)b << 8u);
                if (v > 0u && v < VGM_SHORT_WAIT_QUANTUM) ++short_count;
                st = 0u;
                break;
            }
            case 4u: st = 5u; break;                          /* type byte  */
            case 5u: st = 6u; break;                          /* compat     */
            case 6u: dblk  = (uint32_t)b;        st = 7u; break;
            case 7u: dblk |= (uint32_t)b << 8u;  st = 8u; break;
            case 8u: dblk |= (uint32_t)b << 16u; st = 9u; break;
            case 9u: /* size hi -- transition to body skip */
                dblk |= (uint32_t)b << 24u;
                if (dblk == 0u) { st = 0u; }
                else {
                    /* Skip remaining bytes in this chunk first */
                    uint32_t in_chunk = (uint32_t)(n - i - 1u);
                    if (dblk <= in_chunk) {
                        i += (uint16_t)dblk;
                        dblk = 0u;
                        st = 0u;
                    } else {
                        dblk -= in_chunk;
                        i = n;  /* exhaust chunk, outer loop fast-skips rest */
                        st = 10u;
                    }
                }
                break;
            default: break;
            }

            if (short_count >= VGM_SHORT_WAIT_MAX) {
                textPrint("VGM rejected: sub-sample timing requires offline quantization.\n");
                textPrint("Run: python3 tools/vgm_quantize.py <file> --hz=200\n");
                return false;
            }
        }
    }

    return true;
}
