/*
 * test_far_mvn.c -- incremental far_mvn stress test for F256 Jr.
 *
 * Tests far_mvn by writing a known pattern to increasing physical addresses
 * in extended SRAM (starting at 0x080000), reading it back into near RAM,
 * and verifying every byte.  Starts with a write count of 1 and increases
 * to 255 bytes per chunk.  Within each count, the destination address also
 * advances to exercise the full slot boundary range, especially $Cxxx and
 * $Exxx which previously caused crashes.
 *
 * Faults are reported with the failing address, expected byte, actual byte,
 * and chunk size at the time of failure.  On success each count step prints
 * a green "OK <count>".
 *
 * Build:  make test_far_mvn
 * Run:    copy bin/test_far_mvn.pgz to SD, execute from F256 shell
 */

#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * far_mvn - mvn block move for extended SRAM memory.
 *
 * void far_mvn(uint32_t dest, uint32_t src, uint16_t count)
 * 
 * MVN instruction wraps at 64 KiB bank boundaries.
 * The caller is responsible for ensuring that dest and src are valid physical addresses
 *
 * llvm-mos calling convention (uint32_t, uint32_t, uint16_t):
 *   dest lo:hi:bank  -> A : X : __rc2
 *   src  lo:hi:bank  -> __rc4 : __rc5 : __rc6
 *   count lo:hi      -> __rc8 : __rc9
 *
 * ZP scratch (all __rcN caller-saved in llvm-mos):
 *   __rc6:__rc7  dst 16-bit addr (rebuilt from A:X on entry)
 * ----------------------------------------------------------------------- */
void far_mvn(uint32_t dest, uint32_t src, uint16_t count);
asm(
    ".text\n"
    ".global far_mvn\n"
    "far_mvn:\n"

    "pha\n"
    "stx __rc7\n"
    "lda __rc6\n"
    "sta __tfmvn_src_bank\n"
    "lda __rc2\n"
    "sta __tfmvn_dest_bank\n"
    "pla\n"
    "sta __rc6\n"

    "php\n"
    "sei\n"
    "lda $00\n"
    "ora #$08\n"
    "sta $00\n"
    "lda $01\n"
    "pha\n"            /* save $01                              */
    "ora #$30\n"       /* bits4+5: Moves IO and Cart to Hi-Mem  */
    "sta $01\n"
    "clc\n"
    ".byte $fb\n"      /* XCE */
    ".byte $c2, $30\n" /* REP #$30 */

    "ldx __rc4\n"
    "ldy __rc6\n"
    "lda __rc8\n"
    ".byte $3a\n"      /* DEC A */

    ".byte $54\n"      /* MVN */
    "__tfmvn_dest_bank:\n"
    ".byte $00\n"
    "__tfmvn_src_bank:\n"
    ".byte $00\n"

    "sec\n"
    ".byte $fb\n"      /* XCE */
    "lda $00\n"
    "and #$F7\n"
    "sta $00\n"
    "pla\n"            /* restore $01                           */
    "sta $01\n"
    "plp\n"
    "rts\n"
);

/* -----------------------------------------------------------------------
 * moveup24 -- cross-bank ascending block move using the 65816 MVN instruction.
 *
 * void moveup24(uint32_t dest, uint32_t src, uint16_t count)
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
void moveup24(uint32_t dest, uint32_t src, uint16_t count);
asm(
    ".text\n"
    ".global moveup24\n"
    "moveup24:\n"

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
    "sta __mup24_dst\n"
    "lda __rc10\n"
    "sta __mup24_src\n"

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

    "__mup24_loop:\n"

    "txa\n"
    ".byte $49, $ff, $ff\n"    /* EOR #$FFFF = $FFFF-X                  */
    "sta __rc12\n"

    "tya\n"
    ".byte $49, $ff, $ff\n"    /* EOR #$FFFF = $FFFF-Y                  */
    "cmp __rc12\n"
    "bcc __mup24_have_min\n"
    "lda __rc12\n"
    "__mup24_have_min:\n"

    "cmp __rc8\n"
    "bcc __mup24_do_mvn\n"
    "lda __rc8\n"
    ".byte $3a\n"

    "__mup24_do_mvn:\n"
    "sta __rc12\n"

    ".byte $54\n"              /* MVN dest_bank, src_bank               */
    "__mup24_dst:\n"
    ".byte $00\n"
    "__mup24_src:\n"
    ".byte $00\n"
    ".byte $4b\n"              /* PHK -- push PBR (= 0) onto stack      */
    ".byte $ab\n"              /* PLB -- pull stack top -> DBR = 0      */

    "lda __rc8\n"
    ".byte $3a\n"
    "sec\n"
    "sbc __rc12\n"
    "sta __rc8\n"

    "beq __mup24_done\n"

    ".byte $e0, $00, $00\n"    /* CPX #0                                */
    "bne __mup24_check_dst\n"
    ".byte $e2, $20\n"         /* SEP #$20 -> 8-bit A                   */
    "inc __mup24_src\n"
    ".byte $c2, $20\n"         /* REP #$20 -> 16-bit A                  */

    "__mup24_check_dst:\n"
    ".byte $c0, $00, $00\n"    /* CPY #0                                */
    "bne __mup24_loop\n"
    ".byte $e2, $20\n"         /* SEP #$20 -> 8-bit A                   */
    "inc __mup24_dst\n"
    ".byte $c2, $20\n"         /* REP #$20 -> 16-bit A                  */
    "jmp __mup24_loop\n"

    "__mup24_done:\n"

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
 * far_mvp - mvp block move for extended SRAM memory.
 *
 * void far_mvp(uint32_t dest, uint32_t src, uint16_t count)
 * 
 * MVP instruction wraps at 64 KiB bank boundaries.
 * The caller is responsible for ensuring that dest and src are valid physical addresses
 *
 * llvm-mos calling convention (uint32_t, uint32_t, uint16_t):
 *   dest lo:hi:bank  -> A : X : __rc2
 *   src  lo:hi:bank  -> __rc4 : __rc5 : __rc6
 *   count lo:hi      -> __rc8 : __rc9
 *
 * ZP scratch (all __rcN caller-saved in llvm-mos):
 *   __rc6:__rc7  dst 16-bit addr (rebuilt from A:X on entry)
 * ----------------------------------------------------------------------- */
void far_mvp(uint32_t dest, uint32_t src, uint16_t count);
asm(
    ".text\n"
    ".global far_mvp\n"
    "far_mvp:\n"

    "pha\n"
    "stx __rc7\n"
    "lda __rc6\n"
    "sta __tfmvp_src_bank\n"
    "lda __rc2\n"
    "sta __tfmvp_dest_bank\n"
    "pla\n"
    "sta __rc6\n"

    "php\n"
    "sei\n"
    "lda $00\n"
    "ora #$08\n"
    "sta $00\n"
    "lda $01\n"
    "pha\n"
    "ora #$30\n"        /* bits4+5: Moves IO and Cart to Hi-Mem */
    "sta $01\n"
    "clc\n"
    ".byte $fb\n"       /* XCE -- enter 65816 native mode          */
    ".byte $c2, $30\n"  /* REP #$30 -- 16-bit A, X, Y              */

    /* Compute end addresses: X = src+count-1, Y = dest+count-1 */
    "lda __rc8\n"       /* A = count (16-bit)                      */
    ".byte $3a\n"       /* DEC A -> count - 1                      */
    "sta __rc10\n"      /* scratch: save count-1                   */

    "ldx __rc4\n"       /* X = src 16-bit start                    */
    "txa\n"
    "clc\n"
    "adc __rc10\n"      /* A = src + count - 1                     */
    "tax\n"             /* X = src end address                     */

    "ldy __rc6\n"       /* Y = dest 16-bit start                   */
    "tya\n"
    "clc\n"
    "adc __rc10\n"      /* A = dest + count - 1                    */
    "tay\n"             /* Y = dest end address                    */

    "lda __rc10\n"      /* A = count - 1 (MVP operand)             */
    ".byte $44\n"       /* MVP opcode                              */
    "__tfmvp_dest_bank:\n"
    ".byte $00\n"
    "__tfmvp_src_bank:\n"
    ".byte $00\n"

    "sec\n"
    ".byte $fb\n"       /* XCE -- back to 6502 emulation           */
    "lda $00\n"
    "and #$F7\n"
    "sta $00\n"
    "pla\n"
    "sta $01\n"
    "plp\n"
    "rts\n"
);

/* -----------------------------------------------------------------------
 * Test parameters
 * ----------------------------------------------------------------------- */

/* First extended SRAM block */
#define HIMEM_BASE  0x080000UL

/* Near-RAM staging buffers -- two 512-byte buffers in BSS */
static uint8_t s_src[512];
static uint8_t s_dst[512];

/* -----------------------------------------------------------------------
 * helpers
 * ----------------------------------------------------------------------- */

/* Fill src buffer with a deterministic pattern seeded by `seed`. */
static void fill_pattern(uint8_t *buf, uint16_t len, uint8_t seed)
{
    uint16_t i;
    for (i = 0u; i < len; ++i) {
        buf[i] = (uint8_t)(seed + i);
    }
}

/* Compare dst buffer against expected pattern; return 0 on match, else index+1. */
static uint16_t verify_pattern(const uint8_t *buf, uint16_t len, uint8_t seed)
{
    uint16_t i;
    for (i = 0u; i < len; ++i) {
        if (buf[i] != (uint8_t)(seed + i)) {
            return i + 1u;
        }
    }
    return 0u;
}

/* -----------------------------------------------------------------------
 * far_loop -- 65816 native-mode LDA abs_long,X / STA abs,Y byte loop.
 * Same calling convention as far_mvn but NO MVN/MVP opcode is used.
 * Uses 8-bit A for byte transfers, 16-bit X (src) and Y (dest).
 * DBR is set to dest bank via SEP/PHA/PLB so STA abs,Y hits the right bank.
 * ----------------------------------------------------------------------- */
void far_loop(uint32_t dest, uint32_t src, uint16_t count);
asm(
    ".text\n"
    ".global far_loop\n"
    "far_loop:\n"

    /* prolog: same layout as far_mvn */
    "pha\n"                        /* save dest[7:0]                   */
    "stx __rc7\n"                  /* __rc7  = dest[15:8]               */
    "lda __rc6\n"                  /* A      = src bank                 */
    "sta __tfloop_src_bank\n"      /* self-mod: patch LDA bank byte     */
    "lda __rc2\n"                  /* A      = dest bank                */
    "sta __rc11\n"                 /* __rc11 = dest bank (for PLB)      */
    "pla\n"                        /* A      = dest[7:0]                */
    "sta __rc6\n"                  /* __rc6:__rc7 = dest 16-bit addr    */

    "php\n"
    "sei\n"
    "lda $00\n"
    "ora #$08\n"
    "sta $00\n"                     /* disable MMU                       */
    "lda $01\n"
    "pha\n"
    "ora #$30\n"                    /* bits4+5: match PEEK24/POKE24 $01 mask */
    "sta $01\n"

    /* enter 65816 native mode: 8-bit A, 16-bit X and Y */
    "clc\n"
    ".byte $fb\n"                   /* XCE -> native                     */
    ".byte $e2, $20\n"             /* SEP #$20 -> M=1  (8-bit A)        */
    ".byte $c2, $10\n"             /* REP #$10 -> X=0  (16-bit X, Y)   */

    /* set up address registers */
    "ldx __rc4\n"                  /* X = src 16-bit addr               */
    "ldy __rc6\n"                  /* Y = dest 16-bit addr              */

    /* save byte count (8-bit fits 1..255) */
    "lda __rc8\n"
    "sta __rc10\n"

    /* set DBR = dest bank so STA abs,Y writes to dest bank */
    "lda __rc11\n"                 /* A = dest bank (8-bit)             */
    "pha\n"
    ".byte $ab\n"                   /* PLB: DBR = dest bank              */

    /* reload count */
    "lda __rc10\n"

    /* byte-copy loop
     * A holds the remaining count.  Save it to __rc10 before each LDA
     * because LDA abs_long,X clobbers A with the source byte. */
    "__tfloop_top:\n"
    "sta __rc10\n"                  /* save count before LDA clobbers A */
    ".byte $bf, $00, $00\n"        /* LDA abs_long,X: load src byte     */
    "__tfloop_src_bank:\n"
    ".byte $00\n"                   /* src bank patched in prolog        */
    ".byte $99, $00, $00\n"        /* STA $0000,Y: store to DBR:Y       */
    ".byte $e8\n"                   /* INX (16-bit)                      */
    ".byte $c8\n"                   /* INY (16-bit)                      */
    "lda __rc10\n"                  /* reload count                      */
    ".byte $3a\n"                   /* DEC A (8-bit)                     */
    "bne __tfloop_top\n"

    /* restore DBR = 0: LDA #0 (1 byte pushed), PLB pops it -- no stack leak */
    ".byte $a9, $00\n"             /* LDA #$00 (8-bit immediate)        */
    "pha\n"
    ".byte $ab\n"                   /* PLB: DBR = 0                      */

    /* return to emulation mode and restore state */
    "sec\n"
    ".byte $fb\n"                   /* XCE -> emulation                  */
    "lda $00\n"
    "and #$F7\n"
    "sta $00\n"
    "pla\n"                         /* restore $01                       */
    "sta $01\n"
    "plp\n"
    "rts\n"
);


/* -----------------------------------------------------------------------
 * Shared test runner
 * ----------------------------------------------------------------------- */
typedef void (*mvfn_t)(uint32_t, uint32_t, uint16_t);

static uint8_t run_one_test(mvfn_t fn, char *sl, uint8_t row, bool verbose, bool allow_cross_bank)
{
    uint16_t count;
    uint8_t  pass = 1u;

    for (count = 1u; count <= 255u && pass; ++count) {
        uint32_t stride = (uint32_t)count * 17u;
        uint32_t offset;
        for (offset = 0u; offset < 0x10000UL && pass; offset += stride) {
            uint32_t dest = HIMEM_BASE + offset;
            if (!allow_cross_bank && (offset + (uint32_t)count) > 0x10000UL) {
                continue;
            }
            uint8_t  seed = (uint8_t)(offset ^ count);

            fill_pattern(s_src, count, seed);
            fn(dest, (uint32_t)(uint16_t)(uintptr_t)s_src, count);
            memset(s_dst, 0u, count);
            fn((uint32_t)(uint16_t)(uintptr_t)s_dst, dest, count);

            uint16_t bad = verify_pattern(s_dst, count, seed);
            if (bad) {
                pass = 0u;
                if (verbose) {
                    uint16_t idx = bad - 1u;
                    snprintf(sl, 64, "FAIL cnt=%-3u off=%-5lu idx=%-3u    ",
                             count, (unsigned long)offset, idx);
                    textGotoXY(0, row); textPrint(sl);
                    snprintf(sl, 64, "dest=%-8lu exp=%-3u got=%-3u        ",
                             (unsigned long)(dest + idx),
                             (uint8_t)(seed + idx), s_dst[idx]);
                    textGotoXY(0, (uint8_t)(row + 1u)); textPrint(sl);
                }
            }
        }
        if (pass && verbose && (count & 0xFu) == 0u) {
            snprintf(sl, 64, "cnt=%-3u...                             ", count);
            textGotoXY(0, row); textPrint(sl);
        }
    }
    return pass;
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    textClear();
    char    s_line[64];
    uint8_t r;

    textGotoXY(0, 0); textPrint("MVN/MVP/Loop test  base=0x080000      ");

    /* Run each test 100 times to expose intermittent hardware issues. */
    {
        const uint8_t runs = 100u;
        uint8_t failures = 0u;

        textGotoXY(0, 2); textPrint("--- MVN (ascending) ---               ");
        r = run_one_test(far_mvn, s_line, 3, true, false);
        if (!r) failures++;
        for (uint8_t i = 1u; i < runs; ++i) {
            if (!run_one_test(far_mvn, s_line, 3, true, false)) failures++;
        }
        snprintf(s_line, 64, "MVN: %s (%u/%u failures)        ",
                 failures == 0u ? "ALL PASS" : "FAILED", failures, runs);
        textGotoXY(0, 6); textPrint(s_line);
    }

    {
        const uint8_t runs = 100u;
        uint8_t failures = 0u;

        textGotoXY(0, 8); textPrint("--- moveup24 (ascending, cross-bank) ---");
        r = run_one_test(moveup24, s_line, 9, true, true);
        if (!r) failures++;
        for (uint8_t i = 1u; i < runs; ++i) {
            if (!run_one_test(moveup24, s_line, 9, true, true)) failures++;
        }
        snprintf(s_line, 64, "moveup24: %s (%u/%u failures)     ",
                 failures == 0u ? "ALL PASS" : "FAILED", failures, runs);
        textGotoXY(0, 12); textPrint(s_line);
    }

    /* Cross-bank exercise: copy 512 bytes starting near the end of bank 8. */
    {
        const uint32_t cross_dest = HIMEM_BASE + 0xFF00u;
        const uint16_t cross_len = 512u;
        uint8_t seed = 0xA5u;
        fill_pattern(s_src, cross_len, seed);
        moveup24(cross_dest, (uint32_t)(uint16_t)(uintptr_t)s_src, cross_len);
        memset(s_dst, 0, cross_len);
        moveup24((uint32_t)(uint16_t)(uintptr_t)s_dst, cross_dest, cross_len);
        uint16_t bad = verify_pattern(s_dst, cross_len, seed);
        if (bad) {
            uint16_t idx = bad - 1u;
            snprintf(s_line, 64,
                     "cross-bank FAIL idx=%-3u dest=0x%06lx", idx,
                     (unsigned long)(cross_dest + idx));
            textGotoXY(0, 14); textPrint(s_line);
        } else {
            textGotoXY(0, 14); textPrint("cross-bank OK                             ");
        }
    }

    {
        const uint8_t runs = 100u;
        uint8_t failures = 0u;

        textGotoXY(0, 16); textPrint("--- MVP (descending) ---              ");
        r = run_one_test(far_mvp, s_line, 17, true, false);
        if (!r) failures++;
        for (uint8_t i = 1u; i < runs; ++i) {
            if (!run_one_test(far_mvp, s_line, 17, true, false)) failures++;
        }
        snprintf(s_line, 64, "MVP: %s (%u/%u failures)        ",
                 failures == 0u ? "ALL PASS" : "FAILED", failures, runs);
        textGotoXY(0, 20); textPrint(s_line);
    }

    {
        const uint8_t runs = 100u;
        uint8_t failures = 0u;

        textGotoXY(0, 22); textPrint("--- far_loop (65816 LDA/STA) ---      ");
        r = run_one_test(far_loop, s_line, 23, true, false);
        if (!r) failures++;
        for (uint8_t i = 1u; i < runs; ++i) {
            if (!run_one_test(far_loop, s_line, 23, true, false)) failures++;
        }
        snprintf(s_line, 64, "farloop: %s (%u/%u failures)      ",
                 failures == 0u ? "ALL PASS" : "FAILED", failures, runs);
        textGotoXY(0, 26); textPrint(s_line);
    }

    getchar();
    return 0;
}
