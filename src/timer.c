
#include "f256lib.h"
#include "../include/timer.h"

/* Stored in dot-clock ticks so that variable-rate T0 periods can be
 * subtracted directly without a unit conversion at service time.
 * setAlarm() still accepts T0_TICK_FREQ-Hz tick counts; it converts on entry.
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

static uint8_t alarm_bit(timer_alarm_id_t alarm) {
	return (uint8_t)(1u << ((uint8_t)alarm & 7u));
}

static void serviceTimer0(void) {
	/* In variable-rate (VGM) mode the VGM library owns T0_PEND.
	 * vgm_service() must be the one to consume it.  If we clear it here
	 * first, vgm_service() will never see the fire and will wait forever:
	 * with T0_CMP_CTR=0 (no RECLEAR) T0 only re-matches after a full
	 * 24-bit wrap (~666 ms), causing the lengthy pauses.  Bail out and let
	 * vgm_service() handle everything via timer_tick_elapsed(). */
	if (!g_fixed_rate) {
		return;
	}
	if (!isTimerDone()) {
		return;
	}
	/* Fixed-rate mode: consume the pending flag, service alarms, re-arm. */
	timer_tick_elapsed(g_last_period);
	setTimer0();
}



void timer_service(void) {
	serviceTimer0();
}


void setTimer0()
{
	/* Restore fixed-rate 30 Hz mode.  Called by vgm_close() to hand T0
	 * back to the general alarm subsystem after VGM playback ends.
	 * Uses RECLEAR mode for the 30 Hz tick so timer_service() sees
	 * periodic T0_PEND flags without needing to re-arm each time. */
	g_last_period = T0_TICK_PERIOD_TICKS;
	g_fixed_rate  = true;
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CMP_L, T0_TICK_CMP_L);
	POKE(T0_CMP_M, T0_TICK_CMP_M);
	POKE(T0_CMP_H, T0_TICK_CMP_H);
	POKE(T0_CMP_CTR, T0_CMP_CTR_RECLEAR);
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CTR, CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}

void resetTimer0()
{
	POKE(T0_CMP_CTR, 0);
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CTR, CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}
uint32_t readTimer0()
{
	return (uint32_t)((PEEK(T0_VAL_H)))<<16 | (uint32_t)((PEEK(T0_VAL_M)))<<8 | (uint32_t)((PEEK(T0_VAL_L)));
}

uint32_t readTimer0_consistent(void)
{
	// Attempt a consistent snapshot of the 24-bit counter.
	// If the high byte changes during the read, re-sample the low bytes once.
	for (uint8_t tries = 0u; tries < 2u; ++tries) {
		const uint8_t h1 = PEEK(T0_VAL_H);
		uint8_t m = PEEK(T0_VAL_M);
		uint8_t l = PEEK(T0_VAL_L);
		const uint8_t h2 = PEEK(T0_VAL_H);
		if (h1 == h2) {
			return ((uint32_t)h1 << 16) | ((uint32_t)m << 8) | (uint32_t)l;
		}
		// High byte rolled; re-read M/L (use the later high byte).
		m = PEEK(T0_VAL_M);
		l = PEEK(T0_VAL_L);
		return ((uint32_t)h2 << 16) | ((uint32_t)m << 8) | (uint32_t)l;
	}
	return readTimer0();
}

/* --- New public API -------------------------------------------------- */

void timer_set_period(uint32_t ticks)
{
	/* Program T0 for a one-shot variable-rate period.
	 * Matches the reference player's setTimer0() register sequence exactly:
	 *   1. CTR_CLEAR        → counter to 0
	 *   2. CMP_L/M/H        → load compare value
	 *   3. CMP_CTR = 0      → single-fire, no reclear/reload
	 *   4. CTR_CLEAR         → counter to 0 (redundant but matches reference)
	 *   5. CTR_INTEN|UPDOWN|ENABLE → start counting with interrupt enable
	 *
	 * CTR_INTEN is critical: it enables T0 to assert INT_PENDING_0 bit 4
	 * when the compare match fires.  Without it the pending flag may not
	 * be set reliably, causing the random timing behaviour.
	 *
	 * No overhead compensation — the reference player doesn't use any and
	 * runs smoothly.  The small per-frame dispatch overhead is inaudible. */
	g_last_period = ticks;
	g_fixed_rate  = false;
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CMP_L, (uint8_t)(ticks));
	POKE(T0_CMP_M, (uint8_t)(ticks >> 8u));
	POKE(T0_CMP_H, (uint8_t)(ticks >> 16u));
	POKE(T0_CMP_CTR, 0);
	POKE(T0_CTR, CTR_CLEAR);
	POKE(T0_CTR, CTR_INTEN | CTR_UPDOWN | CTR_ENABLE);
}

void timer_tick_elapsed(uint32_t ticks)
{
	/* Clear the T0 pending flag so that only one caller (VGM service or
	 * timer_service) processes each expiry — whichever arrives first wins
	 * because the second will see T0_PEND already clear and bail out. */
	POKE(T0_PEND, 0x10);
	if (alarm_active_mask == 0u) {
		return;
	}
	for (uint8_t index = 0u; index < TIMER_ALARM_COUNT; ++index) {
		if (alarm_ticks[index] > 0u) {
			alarm_ticks[index] = (alarm_ticks[index] > ticks)
				? alarm_ticks[index] - ticks : 0u;
			if (alarm_ticks[index] == 0u) {
				alarm_active_mask &= (uint8_t)~alarm_bit((timer_alarm_id_t)index);
			}
		}
	}
}

/* --- Existing public API (restored / updated) ------------------------- */

bool isTimerDone()
{
	return (PEEK(T0_PEND) & 0x10) != 0;
}

uint32_t getAlarmTicks(timer_alarm_id_t alarm) {
	serviceTimer0();

	if (alarm >= TIMER_ALARM_COUNT) {
		return 0u;
	}

	return alarm_ticks[alarm];
}

void setAlarm(timer_alarm_id_t alarm, uint16_t ticks) {
	if (alarm >= TIMER_ALARM_COUNT) {
		return;
	}

	serviceTimer0();

	/* Convert caller's 30-Hz tick count to dot-clock ticks so that
	 * alarm_ticks[] can be decremented directly by timer_tick_elapsed()
	 * regardless of whether T0 is running at fixed or variable rate. */
	alarm_ticks[alarm] = (uint32_t)ticks * (uint32_t)T0_TICK_PERIOD_TICKS;

	if (ticks > 0u) {
		alarm_active_mask |= alarm_bit(alarm);
	} else {
		alarm_active_mask &= (uint8_t)~alarm_bit(alarm);
	}

	if (g_fixed_rate) {
		setTimer0();
	}
}

void clearAlarm(timer_alarm_id_t alarm) {
	if (alarm >= TIMER_ALARM_COUNT) {
		return;
	}

	serviceTimer0();

	alarm_ticks[alarm] = 0u;
	alarm_active_mask &= (uint8_t)~alarm_bit(alarm);
}

bool checkAlarm(timer_alarm_id_t alarm) {
	serviceTimer0();

	if (alarm >= TIMER_ALARM_COUNT) {
		return true;
	}

	return (alarm_ticks[alarm] == 0u);
}
