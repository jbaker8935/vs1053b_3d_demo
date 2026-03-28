
#include "f256lib.h"
#include "../include/timer.h"

/* Stored in dot-clock ticks so that variable-rate T0 periods can be
 * subtracted directly without a unit conversion at service time.
 * timer_t0_alarm_set() still accepts T0_TICK_FREQ-Hz tick counts; it converts on entry.
 */
static uint32_t alarm_ticks[TIMER_ALARM_COUNT] = {0};
static uint8_t  alarm_active_mask = 0u;

/* --- T0 mode state --------------------------------------------------- */
/* Period (in dot-clock ticks) that was last programmed into T0.         */
static uint32_t g_last_period = T0_TICK_PERIOD_TICKS;
/* When true  – serviceTimer0() re-arms T0 at T0_TICK_PERIOD_TICKS.     */
/* When false – the VGM library owns T0 re-arming; we only service       */
/*              alarms and clear T0_PEND.                                 */
static bool g_fixed_rate = true;

/* Watchdog counter for T0_PEND miss recovery.
 * Incremented on every timer_t0_is_done() call that sees PEND=0 in variable-rate
 * mode.  When it wraps (every 256 polls) the counter is read as a fallback
 * in case T0_PEND failed to assert (hardware bug, ~1 in 100,000 events).
 * Reset to 0 on each timer_set_period() arm so T0_PEND firing normally
 * (before 256 polls accumulate) keeps the overhead at exactly 1 PEEK/poll. */
static uint8_t g_pend_watchdog = 0u;


static uint8_t timer_alarm_bit(timer_alarm_id_t alarm) {
	return (uint8_t)(1u << ((uint8_t)alarm & 7u));
}

static void timer_t0_irq_mask(void)
{
	/* Polling mode: keep Timer0 masked at the interrupt controller so
	 * compare events do not route to CPU IRQ handlers/kernel IRQ events. */
	uint8_t mask = (uint8_t)PEEK(T0_MASK);
	POKE(T0_MASK, (uint8_t)(mask | T0_PEND_BIT));
}

static void timer_t0_pend_clear(void)
{
	/* Interrupt controller PENDING registers are write-1-to-clear. */
	POKE(T0_PEND, T0_PEND_BIT);
}

static void timer_t0_service(void) {
	/* In variable-rate (VGM) mode the VGM library owns T0_PEND.
	 * vgm_service() must be the one to consume it.  If we clear it here
	 * first, vgm_service() will never see the fire and will wait forever:
	 * with T0_CMP_CTR=0 (no RECLEAR) T0 only re-matches after a full
	 * 24-bit wrap (~666 ms), causing the lengthy pauses.  Bail out and let
	 * vgm_service() handle everything via timer_tick_elapsed(). */
	if (!g_fixed_rate) {
		return;
	}
	if (!timer_t0_is_done()) {
		return;
	}
	/* Fixed-rate mode: consume the pending flag, service alarms, re-arm. */
	timer_t0_tick_elapsed(g_last_period);
	timer_t0_set();
}


void timer_t0_set()
{
	/* Restore fixed-rate 30 Hz mode.  Called by vgm_close() to hand T0
	 * back to the general alarm subsystem after VGM playback ends.
	 * Uses RECLEAR mode for the 30 Hz tick so timer_service() sees
	 * periodic T0_PEND flags without needing to re-arm each time. */
	g_last_period = T0_TICK_PERIOD_TICKS;
	g_fixed_rate  = true;
	timer_t0_irq_mask();
	timer_t0_pend_clear();
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CMP_L, T0_TICK_CMP_L);
	POKE(T0_CMP_M, T0_TICK_CMP_M);
	POKE(T0_CMP_H, T0_TICK_CMP_H);
	POKE(T0_CMP_CTR, T0_CMP_CTR_RECLEAR);
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CTR, CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}

void timer_t0_reset()
{
	timer_t0_irq_mask();
	timer_t0_pend_clear();
	POKE(T0_CMP_CTR, 0);
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CTR, CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}
uint32_t timer_t0_read()
{
	return (uint32_t)((PEEK(T0_VAL_H)))<<16 | (uint32_t)((PEEK(T0_VAL_M)))<<8 | (uint32_t)((PEEK(T0_VAL_L)));
}

uint32_t timer_t0_read_consistent(void)
{

   /* Consistent 24-bit read: retry until high and middle bytes are stable. */
    uint8_t h, m, l, h2, m2;
    do {
        h  = PEEK(T0_VAL_H);
        m  = PEEK(T0_VAL_M);
        l  = PEEK(T0_VAL_L);
        h2 = PEEK(T0_VAL_H);
        m2 = PEEK(T0_VAL_M);
    } while (h2 != h || m2 != m);

    return ((uint32_t)h << 16) | ((uint32_t)m << 8) | (uint32_t)l;

    /* Retry for High byte stability */
	/* Accurate to 10us if M byte rolls during read */
    // uint8_t h1, h2, m, l;
    // do {
    //     h1 = PEEK(T0_VAL_H);
    //     m  = PEEK(T0_VAL_M);
    //     l  = PEEK(T0_VAL_L);
    //     h2 = PEEK(T0_VAL_H);
    // } while (h1 != h2);

    // return ((uint32_t)h1 << 16) | ((uint32_t)m << 8) | (uint32_t)l; 
}

/* --- New public API -------------------------------------------------- */

void timer_period_set(uint32_t ticks)
{
	/* Program T0 for a one-shot variable-rate period.
	 *   1. CTR_CLEAR        → counter to 0
	 *   2. CMP_L/M/H        → load compare value
	 *   3. CMP_CTR = RELOAD → latch compare bytes
	 *   4. CMP_CTR = 0      → single-fire, no reclear/reload
	 *   5. CTR_CLEAR         → counter to 0 (redundant)
	 *   6. CTR_INTEN|UPDOWN|ENABLE → start counting with interrupt enable
	 *
	 * Keep CTR_INTEN set so Timer0 compare events also update INT_PENDING_0
	 * bit 4. This is useful for diagnostics and keeps behavior consistent
	 * with fixed-rate mode. Timer0 remains IRQ-masked via INT_MASK_0.
 */
	ticks &= (uint32_t)T0_MASK_TICKS;
	if (ticks == 0u) {
		ticks = 1u;
	}
	g_last_period    = ticks;
	g_fixed_rate     = false;
	g_pend_watchdog  = 0u;  /* reset before each arm */
	timer_t0_irq_mask();
	timer_t0_pend_clear();
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CMP_L, (uint8_t)(ticks));
	POKE(T0_CMP_M, (uint8_t)(ticks >> 8u));
	POKE(T0_CMP_H, (uint8_t)(ticks >> 16u));
	POKE(T0_CMP_CTR, T0_CMP_CTR_RELOAD);
	POKE(T0_CMP_CTR, 0);  /* one-shot: counter keeps running past match for overrun calc */
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CTR, CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}

void timer_t0_tick_elapsed(uint32_t ticks)
{
	/* Clear the T0 pending flag so that only one caller (VGM service or
	 * timer_service) processes each expiry — whichever arrives first wins
	 * because the second will see T0_PEND already clear and bail out. */
	timer_t0_pend_clear();
	if (alarm_active_mask == 0u) {
		return;
	}
	for (uint8_t index = 0u; index < TIMER_ALARM_COUNT; ++index) {
		if (alarm_ticks[index] > 0u) {
			alarm_ticks[index] = (alarm_ticks[index] > ticks)
				? alarm_ticks[index] - ticks : 0u;
			if (alarm_ticks[index] == 0u) {
				alarm_active_mask &= (uint8_t)~timer_alarm_bit((timer_alarm_id_t)index);
			}
		}
	}
}


/* --- Existing public API (restored / updated) ------------------------- */

bool timer_t0_is_done()
{
	/* Fast path: T0_PEND is reliable 99.999% of the time (1 PEEK, same as
	 * the original implementation).  Reset the watchdog whenever it fires. */
	if ((PEEK(T0_PEND) & T0_PEND_BIT) != 0u) {
		g_pend_watchdog = 0u;
		return true;
	}
	if (g_fixed_rate) {
		return false;
	}
	/* Variable-rate mode:
	 * The watchdog fallback is muted for testing: rely solely on T0_PEND.
	 * The original watchdog increment and counter-check are retained below
	 * but disabled with a preprocessor guard so they can be restored easily. */
#if 0
	/* Variable-rate mode: T0_PEND very rarely fails to assert in one-shot
	 * mode (hardware bug).  Check the counter as a fallback only every 256
	 * polls so the added cost is ~0.4% vs the original single-PEEK path.
	 * At typical polling rates a hardware miss is caught within <2 ms. */
	if (++g_pend_watchdog != 0u) {
		return false;
	}
	/* Watchdog fired (poll 256, 512, ...): verify via counter. */
	return timer_t0_read_consistent() >= g_last_period;
#else
	/* Watchdog muted: do not perform the counter fallback. */
	return false;
#endif
}

uint32_t timer_t0_alarm_ticks_get(timer_alarm_id_t alarm) {
	timer_t0_service();

	if (alarm >= TIMER_ALARM_COUNT) {
		return 0u;
	}

	return alarm_ticks[alarm];
}

void timer_t0_alarm_set(timer_alarm_id_t alarm, uint16_t ticks) {
	if (alarm >= TIMER_ALARM_COUNT) {
		return;
	}

	timer_t0_service();

	/* Convert caller's 30-Hz tick count to dot-clock ticks so that
	 * alarm_ticks[] can be decremented directly by timer_tick_elapsed()
	 * regardless of whether T0 is running at fixed or variable rate. */
	alarm_ticks[alarm] = (uint32_t)ticks * (uint32_t)T0_TICK_PERIOD_TICKS;

	if (ticks > 0u) {
		alarm_active_mask |= timer_alarm_bit(alarm);
	} else {
		alarm_active_mask &= (uint8_t)~timer_alarm_bit(alarm);
	}

	if (g_fixed_rate) {
		timer_t0_set();
	}
}

void timer_t0_alarm_clear(timer_alarm_id_t alarm) {
	if (alarm >= TIMER_ALARM_COUNT) {
		return;
	}

	timer_t0_service();

	alarm_ticks[alarm] = 0u;
	alarm_active_mask &= (uint8_t)~timer_alarm_bit(alarm);
}

bool timer_t0_alarm_check(timer_alarm_id_t alarm) {
	timer_t0_service();

	if (alarm >= TIMER_ALARM_COUNT) {
		return true;
	}

	return (alarm_ticks[alarm] == 0u);
}

