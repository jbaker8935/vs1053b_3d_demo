#if !defined(SRC_TIMER_H__)
#define SRC_TIMER_H__
#include <stdbool.h>
#include <stdint.h>

#define T0_PEND     0xD660
#define T0_MASK     0xD66C
#define T0_PEND_BIT 0x10u

#define T0_CTR      0xD650 // write: Timer0 control register
#define T0_STAT     0xD650 // read: bit0 set when compare is reached

#define CTR_INTEN   0x80  // enable Timer0 compare interrupt/pending generation
#define CTR_ENABLE  0x01
#define CTR_CLEAR   0x02
#define CTR_LOAD    0x04
#define CTR_UPDOWN  0x08

#define T0_VAL_L    0xD651 //current 24 bit value of the timer
#define T0_VAL_M    0xD652
#define T0_VAL_H    0xD653

#define T0_CMP_CTR  0xD654 //b0: t0 returns 0 on reaching target. b1: CMP = last value written to T0_VAL
#define T0_CMP_L    0xD655 //24 bit target value for comparison
#define T0_CMP_M    0xD656
#define T0_CMP_H    0xD657

#define T0_CMP_CTR_RECLEAR 0x01
#define T0_CMP_CTR_RELOAD  0x02

#define T0_WRAP_TICKS 0x1000000u
#define T0_MASK_TICKS 0x00FFFFFFu

#define T0_TICK_FREQ 24
#define VIDEO_DOT_CLOCK_HZ 25175000u
#define T0_TICK_PERIOD_TICKS (VIDEO_DOT_CLOCK_HZ / (uint32_t)T0_TICK_FREQ)
#define T0_TICK_CMP_L ((VIDEO_DOT_CLOCK_HZ/T0_TICK_FREQ)&0xFF)
#define T0_TICK_CMP_M (((VIDEO_DOT_CLOCK_HZ/T0_TICK_FREQ)>>8)&0xFF)
#define T0_TICK_CMP_H (((VIDEO_DOT_CLOCK_HZ/T0_TICK_FREQ)>>16)&0xFF)

typedef uint8_t timer_alarm_id_t;

enum {
	TIMER_ALARM_DEMO_EVENT = 0u,
	TIMER_ALARM_DEMO_AUTO_ADVANCE = 1u,
	TIMER_ALARM_DEMO_ANIM = 2u,
	TIMER_ALARM_GENERAL0 = 3u,
	TIMER_ALARM_COUNT = 4u
};

// Animation callbacks fire every DEMO_ANIM_FRAME_INTERVAL render frames (24 fps at 24 Hz)
#define DEMO_ANIM_FRAME_INTERVAL 1u

void setTimer0(void);
void resetTimer0(void);
uint32_t readTimer0(void);
uint32_t readTimer0_consistent(void);
bool isTimerDone(void);
void timer_service(void);
void setAlarm(timer_alarm_id_t alarm, uint16_t ticks);
void clearAlarm(timer_alarm_id_t alarm);
bool checkAlarm(timer_alarm_id_t alarm);
uint32_t getAlarmTicks(timer_alarm_id_t alarm);

/* Variable-rate T0 sharing -- used by the VGM library.
 *
 * timer_set_period(ticks)
 *   Programs T0 to fire after exactly `ticks` dot-clock ticks and switches
 *   timer.c into variable-rate mode so that timer_service() will NOT re-arm
 *   T0 after the next expiry.  The VGM library calls this for each wait
 *   command in the stream.
 *
 * timer_tick_elapsed(ticks)
 *   Must be called by whichever owner consumed the T0_PEND flag after it
 *   fires.  Clears T0_PEND and subtracts `ticks` from all active general
 *   alarms (saturating at 0) so that alarm accounting remains correct even
 *   while T0 is running at variable rates.
 *
 *   After calling timer_tick_elapsed() the VGM library programs the next
 *   period with timer_set_period().  When VGM playback ends, calling
 *   setTimer0() restores fixed-rate (30 Hz) mode.
 */
void timer_set_period(uint32_t ticks);
void timer_tick_elapsed(uint32_t ticks);

typedef struct {
	uint32_t seq;
	const char *action;
	uint32_t period;
	bool fixed_rate;
	uint8_t pend;
	uint8_t stat;
	uint8_t ctr;
	uint8_t cmp_ctr;
	uint32_t t0_val;
	uint32_t t0_cmp;
} timer_debug_event_t;

const char *timer_debug_last_action(void);
uint32_t timer_debug_last_period(void);
bool timer_debug_is_fixed_rate(void);
uint8_t timer_debug_get_history(timer_debug_event_t *out, uint8_t max_events);

#endif // SRC_TIMER_H__
