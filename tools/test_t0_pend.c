/*
 * test_t0_pend.c -- T0 pending-flag reliability stress test
 *
 * Problem Statement:
 *   When T0 is armed in one-shot mode (T0_CMP_CTR=0) with CTR_INTEN set, the
 *   T0_PEND flag (0xD660 bit 4) sometimes fails to assert even though the
 *   counter has advanced far past the compare target.
 *
 * Evidence from VGM playback (trigger for this test):
 *   counter_value=6695429, compare_target=402037 (16.6x overshoot), pend=0x00
 *   Both T0_PEND and T0_STAT remain 0x00.  Reproducible ~1 in 100,000 arms.
 *
 * What this test does:
 *   Arms T0 with CTR_INTEN in two variants and polls for T0_PEND.  For each
 *   trial it records whether PEND fired before the counter reached the target
 *   (OK), after (LATE), or not at all by 3x the period (MISS).  T0_STAT is
 *   also checked at the miss point.  Up to 16 failure examples are saved.
 *
 * Test phases:
 *   Phase A:  CTR_CLEAR -> CMP_L/M/H -> CMP_CTR=0 -> CTR_CLEAR -> CTR_INTEN|UPDOWN|ENABLE
 *             (minimal one-shot, no RELOAD strobe)
 *   Phase B:  same but with CMP_CTR=RELOAD before CMP_CTR=0
 *             (production sequence – RELOAD strobe to latch compare value)
 *
 * Results are stored in volatile globals for post-mortem via debugger
 * and also displayed on screen.
 *
 * Target: F256 (llvm-mos, mos-f256-clang)
 * Build:  make test_t0_pend
 */

#define F256LIB_IMPLEMENTATION
#include "f256lib.h"

/* -----------------------------------------------------------------------
 * T0 register map
 * ----------------------------------------------------------------------- */
#define T0_PEND     0xD660  /* shared pending reg; T0=bit4(0x10), T1=bit5(0x20) */
#define T0_MASK     0xD66C  /* interrupt mask group 0; bit4 masks Timer0 IRQ route */
#define T0_CTR      0xD650  /* write: master control */
#define T0_STAT     0xD650  /* read:  bit0=compare_reached */
#define T0_VAL_L    0xD651
#define T0_VAL_M    0xD652
#define T0_VAL_H    0xD653
#define T0_CMP_CTR  0xD654  /* b0=RECLEAR, b1=RELOAD */
#define T0_CMP_L    0xD655
#define T0_CMP_M    0xD656
#define T0_CMP_H    0xD657

#define CTR_INTEN   0x80    /* enable interrupt / pending flag on compare match */
#define CTR_ENABLE  0x01
#define CTR_CLEAR   0x02    /* reset counter to 0 */
#define CTR_UPDOWN  0x08    /* 1=count up */
#define CMP_CTR_RELOAD  0x02

/* T1: free-running reference clock (no interrupts, no compare) */
#define T1_CTR      0xD658
#define T1_VAL_L    0xD659
#define T1_VAL_M    0xD65A
#define T1_VAL_H    0xD65B

/* -----------------------------------------------------------------------
 * Test parameters
 *
 * N_PERIODS periods are tested per phase.
 * Trial counts are chosen so total runtime is ~30 s at 6 MHz CPU.
 * At 1-in-100,000 failure rate, the 50,000-trial period should catch ~0-1;
 * the 571-tick period has the most trials and highest absolute miss count.
 * ----------------------------------------------------------------------- */
#define N_PERIODS   4u

/* Compare targets (dot-clock ticks at 25.175 MHz) */
static const uint32_t TEST_CMP[N_PERIODS] = {
    571u,       /* 1 VGM sample  (~22.7 µs) */
    5710u,      /* 10 VGM samples (~227 µs) */
    57100u,     /* 100 VGM samples (~2.3 ms) */
    402037u,    /* real-world stall period ~16 ms */
};

/* Trials per period (more trials at short periods where we can afford it) */
static const uint32_t TEST_TRIALS[N_PERIODS] = {
    500000u,
    100000u,
    20000u,
    2500u,
};

/*
 * OK_SLACK_TICKS: dotclock ticks allowed past cmp before classifying as LATE.
 * CPU is a 12 MHz 65816 in 6502 emulation mode; dotclock is 25.175 MHz
 * → ≈ 2.1 dotclock ticks per CPU cycle.
 * read_t0() worst-case = 8 PEEK (LDA abs to MMIO, ~6-7 cycles each with wait
 * states) + loop overhead ≈ 120 CPU cycles → ~252 dotclock ticks.
 * 256 is just above the observed worst-case and remains well below the
 * 571-tick minimum test period.
 */
#define OK_SLACK_TICKS  256u

/* -----------------------------------------------------------------------
 * Failure log
 * ----------------------------------------------------------------------- */
#define N_FAIL_LOG  16u

typedef struct {
    uint32_t cmp;       /* programmed compare target */
    uint32_t val;       /* T0_VAL at timeout (how far past the target) */
    uint8_t  pend;      /* raw T0_PEND byte */
    uint8_t  stat;      /* raw T0_STAT byte */
    uint8_t  cmp_ctr;   /* T0_CMP_CTR readback (check it stayed 0x00) */
    uint8_t  phase;     /* 'A' or 'B' */
} fail_t;

/* -----------------------------------------------------------------------
 * Result accumulators (volatile for debugger inspection)
 *
 * OK   = PEND fired while counter < cmp (ideal)
 * LATE = PEND fired after counter passed cmp (timing slack)
 * MISS = PEND never fired by the 3x timeout
 * STAT_AT_MISS = T0_STAT was also clear at the timeout point
 * ----------------------------------------------------------------------- */
volatile uint32_t g_ok_A[N_PERIODS];
volatile uint32_t g_late_A[N_PERIODS];
volatile uint32_t g_miss_A[N_PERIODS];
volatile uint32_t g_stat_set_at_miss_A[N_PERIODS];

volatile uint32_t g_ok_B[N_PERIODS];
volatile uint32_t g_late_B[N_PERIODS];
volatile uint32_t g_miss_B[N_PERIODS];
volatile uint32_t g_stat_set_at_miss_B[N_PERIODS];

volatile fail_t   g_fails[N_FAIL_LOG];
volatile uint8_t  g_fail_count = 0u;
volatile uint8_t  g_saved_int_mask_0 = 0u;

/* T1 elapsed ticks at start/end of each phase (for overall timing) */
volatile uint32_t g_t1_start;
volatile uint32_t g_t1_end;

/* -----------------------------------------------------------------------
 * Low-level helpers
 * ----------------------------------------------------------------------- */

static uint32_t read_t0(void)
{
    /* Consistent 24-bit read: retry once if high byte changed mid-read */
    uint8_t h = PEEK(T0_VAL_H);
    uint8_t m = PEEK(T0_VAL_M);
    uint8_t l = PEEK(T0_VAL_L);
    uint8_t h2 = PEEK(T0_VAL_H);
    if (h2 != h) {
        h = h2;
        m = PEEK(T0_VAL_M);
        l = PEEK(T0_VAL_L);
    }
    return ((uint32_t)h << 16) | ((uint32_t)m << 8) | (uint32_t)l;
}

static uint32_t read_t1(void)
{
    uint8_t h = PEEK(T1_VAL_H);
    uint8_t m = PEEK(T1_VAL_M);
    uint8_t l = PEEK(T1_VAL_L);
    uint8_t h2 = PEEK(T1_VAL_H);
    if (h2 != h) {
        h = h2;
        m = PEEK(T1_VAL_M);
        l = PEEK(T1_VAL_L);
    }
    return ((uint32_t)h << 16) | ((uint32_t)m << 8) | (uint32_t)l;
}

static void log_fail(uint32_t cmp, uint32_t val, uint8_t pend, uint8_t stat,
                     uint8_t cmp_ctr, uint8_t phase)
{
    if (g_fail_count >= N_FAIL_LOG) { return; }
    uint8_t i = g_fail_count++;
    g_fails[i].cmp     = cmp;
    g_fails[i].val     = val;
    g_fails[i].pend    = pend;
    g_fails[i].stat    = stat;
    g_fails[i].cmp_ctr = cmp_ctr;
    g_fails[i].phase   = phase;
}

/* -----------------------------------------------------------------------
 * Arm T0 in one-shot mode with CTR_INTEN.
 * use_reload=true  → issue CMP_CTR=RELOAD before CMP_CTR=0 (Phase B)
 * use_reload=false → skip RELOAD strobe                      (Phase A)
 * ----------------------------------------------------------------------- */
static void arm_t0(uint32_t cmp, bool use_reload)
{
    POKE(T0_PEND,  0x10);                               /* clear stale pending */
    POKE(T0_CTR,   CTR_CLEAR);                          /* reset counter to 0 */
    POKE(T0_CMP_L, (uint8_t)(cmp));
    POKE(T0_CMP_M, (uint8_t)(cmp >> 8));
    POKE(T0_CMP_H, (uint8_t)(cmp >> 16));
    if (use_reload) {
        POKE(T0_CMP_CTR, CMP_CTR_RELOAD);               /* latch compare value */
    }
    POKE(T0_CMP_CTR, 0);                                /* one-shot mode */
    POKE(T0_CTR,   CTR_CLEAR);                          /* zero counter (redundant) */
    POKE(T0_CTR,   CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}

/* -----------------------------------------------------------------------
 * run_trial
 *
 * Returns:  0 = PEND_OK   (PEND fired before/at counter reaching cmp)
 *           1 = PEND_LATE (PEND fired after counter passed cmp, within 3x)
 *           2 = PEND_MISS (PEND never fired by 3x timeout)
 *
 * On MISS also captures and logs a failure example if slots remain.
 * ----------------------------------------------------------------------- */
static uint8_t run_trial(uint32_t cmp, bool use_reload)
{
    arm_t0(cmp, use_reload);

    /* --- Phase 1: tight poll until PEND fires or counter passes the target - */
    for (;;) {
        if (PEEK(T0_PEND) & 0x10u) {
            return 0;           /* PEND fired; counter is still ≤ cmp or just past */
        }
        uint32_t t0_val = read_t0();
        if (t0_val >= cmp) {
            /* Allow a small overrun caused by read_t0() instruction timing:
             * if still within OK_SLACK_TICKS of cmp and PEND has now set,
             * treat as OK rather than LATE. */
            if ((t0_val - cmp) < OK_SLACK_TICKS && (PEEK(T0_PEND) & 0x10u)) {
                return 0;
            }
            break;              /* counter overtook compare with no PEND */
        }
    }

    /* --- Phase 2: extended poll – give PEND up to 3× cmp to arrive -------- */
    uint32_t timeout = cmp + cmp + cmp;
    if (timeout < cmp) { timeout = 0x00FFFFFFu; }  /* 24-bit overflow guard */

    for (;;) {
        if (PEEK(T0_PEND) & 0x10u) {
            return 1;           /* arrived late */
        }
        uint32_t val = read_t0();
        if (val >= timeout) {
            /* Definitive miss – capture evidence */
            uint8_t pend    = PEEK(T0_PEND);
            uint8_t stat    = PEEK(T0_STAT);
            uint8_t cmp_ctr = PEEK(T0_CMP_CTR);
            log_fail(cmp, val, pend, stat, cmp_ctr,
                     use_reload ? (uint8_t)'B' : (uint8_t)'A');
            return 2;
        }
    }
}

/* -----------------------------------------------------------------------
 * run_phase  -- run all periods for one phase variant, store into out arrays
 * ----------------------------------------------------------------------- */
static void run_phase(bool use_reload,
                      volatile uint32_t ok[],
                      volatile uint32_t late[],
                      volatile uint32_t miss[],
                      volatile uint32_t stat_at_miss[])
{
    for (uint8_t p = 0u; p < N_PERIODS; ++p) {
        ok[p] = 0u; late[p] = 0u; miss[p] = 0u; stat_at_miss[p] = 0u;

        uint32_t cmp    = TEST_CMP[p];
        uint32_t trials = TEST_TRIALS[p];

        for (uint32_t t = 0u; t < trials; ++t) {
            uint8_t r = run_trial(cmp, use_reload);
            if (r == 0u) {
                ok[p]++;
            } else if (r == 1u) {
                late[p]++;
            } else {
                miss[p]++;
                /* Check T0_STAT reading immediately after miss was logged */
                if (g_fail_count > 0u) {
                    uint8_t last = g_fail_count - 1u;
                    if (g_fails[last].stat & 0x01u) {
                        stat_at_miss[p]++;
                    }
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Display helpers
 * ----------------------------------------------------------------------- */
static uint8_t g_row;

static void nl(void)
{
    g_row++;
}

static void print_label(uint8_t col, const char *s)
{
    textGotoXY(col, g_row);
    textPrint((char *)s);
}

static void print_u(uint8_t col, uint32_t v)
{
    textGotoXY(col, g_row);
    textPrintUInt(v);
}

/* Print one result row: phase label, period, trials, ok, late, miss, stat@miss */
static void print_result_row(const char *phase_label, uint8_t pidx,
                              volatile uint32_t ok[],
                              volatile uint32_t late[],
                              volatile uint32_t miss[],
                              volatile uint32_t stat_at_miss[])
{
    print_label(0,  phase_label);
    print_u    (3,  TEST_CMP[pidx]);
    print_u    (13, TEST_TRIALS[pidx]);
    print_u    (20, ok[pidx]);
    print_u    (28, late[pidx]);
    print_u    (35, miss[pidx]);
    print_u    (41, stat_at_miss[pidx]);
    nl();
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Polling-only test: keep Timer0 masked at the interrupt controller so
     * compare events do not route into any IRQ handler during stress loops. */
    g_saved_int_mask_0 = PEEK(T0_MASK);
    POKE(T0_MASK, (uint8_t)(g_saved_int_mask_0 | 0x10u));

    /* Start T1 as free-running elapsed clock (no interrupt, no compare) */
    POKE(T1_CTR, CTR_CLEAR);
    POKE(T1_CTR, CTR_UPDOWN | CTR_ENABLE);
    POKE(T0_PEND, 0x20);  /* clear any T1 pending */

    textClear();
    g_row = 0u;

    print_label(0, "T0_PEND RELIABILITY TEST  (hardware bug investigation)");
    nl();
    print_label(0, "T0_PEND=0xD660 bit4  T0_STAT=0xD650 bit0  CTR_INTEN=0x80");
    nl(); nl();

    print_label(0, "Ph CMP       Trials  OK      LATE    MISS   STAT@MISS");
    nl();
    print_label(0, "-- --------  ------  ------  ------  -----  ---------");
    nl();

    /* Save header row count so we can re-print results in-place */
    uint8_t results_start_row = g_row;

    /* -------------------------------------------------------------------- */
    /* Phase A: minimal one-shot, no RELOAD strobe                          */
    /* -------------------------------------------------------------------- */
    //print_label(0, "Running Phase A (no reload strobe)...");
    // nl();
    g_t1_start = read_t1();

    run_phase(false, g_ok_A, g_late_A, g_miss_A, g_stat_set_at_miss_A);

    g_t1_end = read_t1();

    /* Overwrite "Running..." line with results */
    g_row = results_start_row;
    for (uint8_t p = 0u; p < N_PERIODS; ++p) {
        print_result_row("A:", p, g_ok_A, g_late_A, g_miss_A, g_stat_set_at_miss_A);
    }

    nl();

    /* -------------------------------------------------------------------- */
    /* Phase B: with RELOAD strobe (production sequence)                    */
    /* -------------------------------------------------------------------- */
    uint8_t phaseB_start_row = g_row;
    //print_label(0, "Running Phase B (with reload strobe)...");
    //nl();

    run_phase(true, g_ok_B, g_late_B, g_miss_B, g_stat_set_at_miss_B);

    g_row = phaseB_start_row;
    for (uint8_t p = 0u; p < N_PERIODS; ++p) {
        print_result_row("B:", p, g_ok_B, g_late_B, g_miss_B, g_stat_set_at_miss_B);
    }

    nl();

    /* -------------------------------------------------------------------- */
    /* Totals                                                                */
    /* -------------------------------------------------------------------- */
    uint32_t total_miss = 0u;
    for (uint8_t p = 0u; p < N_PERIODS; ++p) {
        total_miss += g_miss_A[p] + g_miss_B[p];
    }

    print_label(0, "Total PEND misses across both phases: ");
    textPrintUInt(total_miss);
    nl();
    print_label(0, "Total fail log entries: ");
    textPrintUInt((uint32_t)g_fail_count);
    nl();
    nl();

    /* -------------------------------------------------------------------- */
    /* Failure log dump                                                      */
    /* -------------------------------------------------------------------- */
    if (g_fail_count == 0u) {
        print_label(0, "No PEND misses recorded (try increasing TEST_TRIALS).");
        nl();
    } else {
        print_label(0, "MISS EXAMPLES (phase, cmp, val_at_timeout, pend, stat, cmp_ctr):");
        nl();
        for (uint8_t i = 0u; i < g_fail_count; ++i) {
            textGotoXY(0, g_row);
            textPrint((char *)"#");
            textPrintUInt((uint32_t)(i + 1u));
            textGotoXY(4,  g_row); textPrint((char *)"ph=");
            textGotoXY(7,  g_row);
            /* print phase character */
            {
                char phbuf[2] = { (char)g_fails[i].phase, '\0' };
                textPrint(phbuf);
            }
            textGotoXY(9,  g_row); textPrint((char *)"cmp=");
            print_u    (13, g_fails[i].cmp);
            textGotoXY(21, g_row); textPrint((char *)"val=");
            print_u    (25, g_fails[i].val);
            textGotoXY(33, g_row); textPrint((char *)"pnd=");
            print_u    (37, (uint32_t)g_fails[i].pend);
            textGotoXY(40, g_row); textPrint((char *)"stt=");
            print_u    (44, (uint32_t)g_fails[i].stat);
            textGotoXY(47, g_row); textPrint((char *)"cctr=");
            print_u    (52, (uint32_t)g_fails[i].cmp_ctr);
            nl();
        }
    }

    nl();
    print_label(0, "T1 elapsed ticks (phase A+B): ");
    print_u(30, g_t1_end - g_t1_start);
    nl();
    print_label(0, "DONE. Inspect g_fails[] + g_miss_A/B[] in debugger.");
    nl();

    getchar();  /* wait for keypress before exit so results stay on screen */

    /* Restore caller/kernel interrupt mask state before returning. */
    POKE(T0_MASK, g_saved_int_mask_0);

    return 0;
}
