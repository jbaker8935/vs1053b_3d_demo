/*
 * bench_mul.c - microbenchmark for shift/add vs coprocessor multiply
 *
 * This harness is intended to run on the F256 target and use the hardware
 * timer (T0) to compare execution cost between:
 *   - a shift/add multiply by constant (samples_to_ticks)
 *   - the coprocessor multiply (mathUnsignedMultiply)
 *
 * The results are stored in volatile globals so they can be inspected
 * post-mortem via a debugger or symbol dump.
 */

#define F256LIB_IMPLEMENTATION
#include "f256lib.h"
#include "timer.h"

/* Match the constant used in src/vgm.c */
#define VGM_TICKS_PER_SAMPLE 571u

// Timer 1
#define T1_PEND 0xD660
#define T1_MASK 0xD66C

#define T1_CTR 0xD658  // master control register for timer1, write.b0=ticks b1=reset b2=set to last value of VAL b3=set count up, clear count down
#define T1_STAT 0xD658 // master control register for timer1, read biT1 set = reached target val

#define T1_VAL_L 0xD659 // current 24 bit value of the timer
#define T1_VAL_M 0xD65A
#define T1_VAL_H 0xD65B

#define T1_CMP_CTR 0xD65C // b0: T1 returns 0 on reaching target. b1: CMP = last value written to T1_VAL
#define T1_CMP_L 0xD65D   // 24 bit target value for comparison
#define T1_CMP_M 0xD65E
#define T1_CMP_H 0xD65F

#define T1_CMP_CTR_RECLEAR 0x01
#define T1_CMP_CTR_RELOAD 0x02

/* Minimal timer helpers used by the benchmark. */
static void benchSetTimer1(void) {
    POKE(T1_CTR, CTR_CLEAR);
    POKE(T1_CTR, CTR_UPDOWN | CTR_ENABLE);
    POKE(T1_PEND, 0x20);  // clear pending timer 1.
}

static uint32_t benchReadTimer1(void) {
    return (uint32_t)((PEEK(T1_VAL_H))) << 16 |
           (uint32_t)((PEEK(T1_VAL_M))) << 8 |
           (uint32_t)((PEEK(T1_VAL_L)));
}

/* This matches the shift/add version in src/vgm.c. */
static uint32_t samples_to_ticks(uint16_t s)
{
    uint32_t v = (uint32_t)s;
    return (v << 9u) + (v << 5u) + (v << 4u) + (v << 3u) + (v << 1u) + v;
}

volatile uint32_t bench_start_ticks;
volatile uint32_t bench_mid_ticks;
volatile uint32_t bench_end_ticks;
volatile uint32_t bench_accumulator;

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    uint32_t acc = 0u;

    benchSetTimer1();

    bench_start_ticks = benchReadTimer1();
    textGotoXY(0, 0);
    textPrint("Running benchmark...");
    textPrintUInt(bench_start_ticks);

    /* Measure shift/add cost */
    for (uint32_t i = 0u; i < 100000u; ++i) {
        acc += samples_to_ticks((uint16_t)i);
    }

    bench_mid_ticks = benchReadTimer1();
    textGotoXY(0, 2);
    textPrint("Mid Ticks: ");
    textPrintUInt(bench_mid_ticks);
    /* Measure coprocessor multiply cost */
    for (uint32_t i = 0u; i < 100000u; ++i) {
        acc += mathUnsignedMultiply((uint16_t)i, (uint16_t)VGM_TICKS_PER_SAMPLE);
    }

    bench_end_ticks = benchReadTimer1();
    textGotoXY(0, 4);
    textPrint("End Ticks: ");
    textPrintUInt(bench_end_ticks);
    bench_accumulator = acc;

    /* Print results via text output */
    // textClear();
    textGotoXY(0, 5);
    textPrint("shift-add:");
    textPrintUInt(bench_mid_ticks - bench_start_ticks);
    textGotoXY(0, 6);
    textPrint("coproc :");
    textPrintUInt(bench_end_ticks - bench_mid_ticks);

    getchar();  // Wait for a keypress so results can be inspected.

    return 0;
}

