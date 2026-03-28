/*
 * vgm.c -- lightweight VGM streaming player for F256 / YMF262 (OPL3)
 *
 *
 * Hardware register map from:
 *   https://f256wiki.wildbitscomputing.com/index.php?title=Use_the_OPL3_YMF262
 *
 * T0 timer shared with timer.c via timer_set_period() / timer_tick_elapsed().
 */

#include "f256lib.h"


#include "../include/timer.h"
#include "../include/vgm.h"

/* -----------------------------------------------------------------------
 * OPL3 (YMF262) hardware registers -- F256 memory-mapped I/O
 * ----------------------------------------------------------------------- */
#define OPL_ADDR_L  0xD580u  /* port-0 address register (regs 0x000-0x0FF) */
#define OPL_DATA    0xD581u  /* shared data register                        */
#define OPL_ADDR_H  0xD582u  /* port-1 address register (regs 0x100-0x1FF) */

/* -----------------------------------------------------------------------
 * Global VGM player state
 * ----------------------------------------------------------------------- */
static uint8_t   vgm_flags;
static uint8_t   vgm_opl_mode;
static uint8_t   vgm_buf_pos;
static uint8_t   vgm_buf_len;
static uint32_t  vgm_wait_carry;
static uint32_t  vgm_catchup_debt_ticks;
static uint32_t  vgm_last_period;
static uint32_t  vgm_samples_elapsed;
static uint32_t  vgm_total_samples;
static uint32_t  vgm_loop_offset;
static uint32_t  vgm_data_start;
static uint32_t  vgm_stream_pos;
static vgm_read_fn vgm_read_fn_ptr;
static vgm_seek_fn vgm_seek_fn_ptr;
static void *vgm_io_ctx;
static uint8_t vgm_buf[VGM_BUF_SIZE];

/* -----------------------------------------------------------------------
 * VGM timing
 *
 * VGM sample rate is 44100 Hz.  T0 is clocked from the video dot clock at
 * 25,175,000 Hz.  Ticks per VGM sample = 25175000 / 44100 = 570.86..., so
 * we use 571.  The tiny per-sample rounding (~0.024 %) accumulates to about
 * ~14.5 ms per minute of playback -- imperceptible on real audio.
 * ----------------------------------------------------------------------- */
#define VGM_TICKS_PER_SAMPLE 571u

/* Maximum value that fits in T0's 24-bit compare register */
#define T0_MAX_TICKS 0x00FFFFFFu

/* Precomputed tick counts for the two most common wait commands.
 * 571 = 512 + 32 + 16 + 8 + 2 + 1, so compile-time evaluation avoids
 * calling the soft __mulsi3 routine at run time. */
#define TICKS_ONE_NTSC  ((uint32_t)735u * VGM_TICKS_PER_SAMPLE)  /* 0x066525 */
#define TICKS_ONE_PAL   ((uint32_t)882u * VGM_TICKS_PER_SAMPLE)  /* 0x07AEC6 */
/* Special Case 0x61 variable timing values.  Used to avoid a multiply  */
#define TICKS_100HZ     ((uint32_t)441u * VGM_TICKS_PER_SAMPLE)  /* 0x3D5EB */
#define TICKS_200HZ     ((uint32_t)220u * VGM_TICKS_PER_SAMPLE)  /* 0x1EAF4 */

/* -----------------------------------------------------------------------
 * OPL3 helpers
 * ----------------------------------------------------------------------- */

/* Write one OPL3 register on port 0. */
static void opl_write_port0(uint8_t reg, uint8_t val)
{
    POKE(OPL_ADDR_L, reg);
    POKE(OPL_DATA, val);
}

/* Write one OPL3 register on port 1. */
static void opl_write_port1(uint8_t reg, uint8_t val)
{
    POKE(OPL_ADDR_H, reg);
    POKE(OPL_DATA, val);
}

/* Busy-wait for exactly `ticks` dot-clock ticks using the free-running T0
 * counter.  Only called for micro-waits (≤ VGM_MIN_MICRO_WAIT_TICKS) during
 * catch-up to preserve OPL3 stereo L/R write-pair spacing.
 *
 * The 24-bit masked subtraction handles counter wraparound correctly for any
 * duration well under the full 24-bit period. */
static void spin_wait(uint32_t ticks)
{
    uint32_t start = timer_t0_read_consistent();
    while (((timer_t0_read_consistent() - start) & T0_MASK_TICKS) < ticks) {
        /* busy wait */
    }
}

/* Key-off all 18 OPL3 channels (9 per bank).
 * Uses the current key/fnum register value but clears the key-on bit.
 * This matches the behavior of the reference opl3_quietAll() implementation
 * and avoids relying on surrounding state (block/fnum) for silence. */
static void opl_silence(void)
{
    uint8_t i;
    for (i = 0u; i < 9u; ++i) {
        opl_write_port0((uint8_t)(0xB0u + i), 0xDFu);  /* key-off, keep block/fnum high */
    }
    for (i = 0u; i < 9u; ++i) {
        opl_write_port1((uint8_t)(0xB0u + i), 0xDFu);  /* key-off, keep block/fnum high */
    }
}

/* Minimal OPL3 chip initialisation.
 *   mode == 3  → enable OPL3 extended feature set (reg 0x105 = 1)
 *   mode == 2  → OPL2 compatible (reg 0x105 = 0)
 */
static void opl_init(uint8_t mode)
{
    uint8_t i;

    /* Enable waveform select (required even in OPL2 mode) */
    opl_write_port0(0x01u, 0x20u);

    /* Disable four-operator modes */
    opl_write_port1(0x04u, 0x00u);

    /* OPL3 enable / disable */
    opl_write_port1(0x05u, (mode == 3u) ? 0x01u : 0x00u);

    /* Percussion/vibrato/tremolo off */
    opl_write_port0(0xBDu, 0x00u);

    /* Enable left+right output on all channels (both banks) */
    for (i = 0u; i < 9u; ++i) {
        opl_write_port0((uint8_t)(0xC0u + i), 0x30u);
        opl_write_port1((uint8_t)(0xC0u + i), 0x30u);
    }

    /* Key-off all channels */
    opl_silence();
}

/* -----------------------------------------------------------------------
 * Stream buffer helpers
 * ----------------------------------------------------------------------- */

/* Refill the stream buffer using the client read callback.  Resets buf_pos
 * and advances stream_pos past the newly-loaded bytes. */
static void buf_refill(void)
{
    uint16_t n = vgm_read_fn_ptr(vgm_io_ctx, vgm_buf, VGM_BUF_SIZE);
    vgm_buf_len = n;
    vgm_buf_pos = 0u;
    vgm_stream_pos += (uint32_t)n;
}

static __attribute__((noinline)) uint8_t buf_refill_and_get(void)
{
    buf_refill();
    return vgm_buf[vgm_buf_pos++];
}

static __attribute__((always_inline)) uint8_t buf_get(void)
{
    if (__builtin_expect(vgm_buf_pos < vgm_buf_len, 1)) {
        return vgm_buf[vgm_buf_pos++];
    }
    return buf_refill_and_get();
}

static void buf_skip(uint8_t n)
{
    uint8_t avail = vgm_buf_len - vgm_buf_pos;
    if (n <= avail) {
        vgm_buf_pos += n;
    } else {
        uint32_t cur = vgm_stream_pos - (uint32_t)vgm_buf_len + (uint32_t)vgm_buf_pos;
        vgm_seek_fn_ptr(vgm_io_ctx, cur + (uint32_t)n);
        vgm_stream_pos = cur + (uint32_t)n;
        vgm_buf_len = 0u;
        vgm_buf_pos = 0u;
        buf_refill();
    }
}

static void buf_seek(uint32_t offset)
{
    vgm_seek_fn_ptr(vgm_io_ctx, offset);
    vgm_stream_pos = offset;
    vgm_buf_len = 0u;
    vgm_buf_pos = 0u;
    buf_refill();
}

static uint16_t buf_get_le16(void)
{
    uint8_t lo = buf_get();
    uint8_t hi = buf_get();
    return (uint16_t)lo | ((uint16_t)hi << 8u);
}

static uint32_t buf_get_le32(void)
{
    uint32_t v  = (uint32_t)buf_get();
    v |= (uint32_t)buf_get() << 8u;
    v |= (uint32_t)buf_get() << 16u;
    v |= (uint32_t)buf_get() << 24u;
    return v;
}


/* -----------------------------------------------------------------------
 * Wait / timer helpers
 * ----------------------------------------------------------------------- */

/* Multiply a 16-bit sample count by VGM_TICKS_PER_SAMPLE (571) using
 * shifts and adds instead of a general-purpose 32-bit software multiply.
 *   571 = 512 + 32 + 16 + 8 + 2 + 1  (= 2^9 + 2^5 + 2^4 + 2^3 + 2^1 + 2^0)
 * Max input 65535 → max result 37,440,585 which fits comfortably in uint32_t. */
static uint32_t samples_to_ticks(uint16_t s)
{
    return mathUnsignedMultiply(s, (uint16_t)VGM_TICKS_PER_SAMPLE);
}

/* Precomputed ticks for the 16 short-wait lengths used by commands 0x70-0x8F.
 * short_wait_ticks[n] = samples_to_ticks(n) for n = 0..16. */
static const uint32_t short_wait_ticks[17u] = {
    0u,     571u,  1142u,  1713u,  2284u,  2855u,  3426u,  3997u,
    4568u, 5139u,  5710u,  6281u,  6852u,  7423u,  7994u,  8565u,  9136u
};

/* Cap VGM waits to one animation frame so that the general-purpose alarm
 * (used by the main loop for 24 Hz frame pacing) is serviced at least once
 * per frame even during long VGM silence or pause commands.
 *
 * The alarm subsystem only decrements alarm_ticks via timer_tick_elapsed(),
 * which is called here when T0 fires.  If a VGM wait exceeds one frame
 * period T0 would not fire until the full wait elapsed, freezing the frame
 * alarm for that duration and causing visible animation hitching.
 *
 * T0_TICK_PERIOD_TICKS (1,048,958) is well within the 24-bit hardware limit
 * (T0_MAX_TICKS = 0xFFFFFF = 16,777,215), so the hardware constraint is
 * trivially satisfied by this tighter software cap.
 *
 * The existing wait_carry mechanism re-schedules the remainder automatically,
 * so long VGM waits are transparently split across frame boundaries with no
 * change to audio timing.
 */
#define VGM_MAX_WAIT_TICKS  T0_TICK_PERIOD_TICKS

/* Minimum T0 period used as a bus-cycle gap during catch-up.
 * Must be >= YMF262 T4 minimum (~506 dot-clock ticks); 1024 gives 2x margin. */
#define VGM_CATCHUP_TICKS 1024u

/* Maximum catch-up rate expressed as a divisor of the nominal wait period.
 * During debt drain each VGM tick is compressed to at most
 * 1/VGM_CATCHUP_DIVISOR of its nominal duration, never below VGM_CATCHUP_TICKS.
 *
 * furnace_bgm.vgm creates stereo depth by triggering the same note on an
 * R-only channel one or two 441-sample (10 ms) ticks before the matching
 * L-only channel note.  Compressing those ticks to VGM_CATCHUP_TICKS (40 µs)
 * collapses the delay to <100 µs -- inaudible.  With divisor=10 each 10 ms
 * tick takes at least 1 ms even during heavy catch-up, so a 2-tick stereo
 * delay remains ~2 ms -- clearly perceivable. */
#define VGM_CATCHUP_DIVISOR 10u

/* Define to 1 to enable catch-up proportional compression (default behavior)
 * Define to 0 to disable proportional timing compression and drain waits in
 * nominal duration instead. */
#define VGM_CATCHUP_COMPRESSION 0

/* Micro-waits at or below this threshold are honoured in full (by busy-wait)
 * even during catch-up.  Waits this short separate OPL3 port-0 / port-1 write
 * pairs for stereo imaging; collapsing them destroys L/R channel separation.
 * 4 samples × 571 ticks/sample = 2284 ticks ≈ 90 µs. */
#define VGM_MIN_MICRO_WAIT_TICKS ((uint32_t)4u * VGM_TICKS_PER_SAMPLE)

static __attribute__((always_inline)) bool schedule_wait_arm(uint32_t ticks)
{
    /* Frame-cap: split waits longer than one frame so the general alarm
     * subsystem is serviced at least once per frame (see VGM_MAX_WAIT_TICKS). */
    if (ticks > VGM_MAX_WAIT_TICKS) {
        vgm_wait_carry = ticks - VGM_MAX_WAIT_TICKS;
        ticks = VGM_MAX_WAIT_TICKS;
    } else {
        vgm_wait_carry = 0u;
    }
    vgm_last_period = ticks;
    timer_period_set(ticks);
    vgm_flags |= VGM_FLAG_TIMER_RUN;
    return true;
}

/* Two-stage overrun compensation.
 *
 * Stage 1 (COMPENSATE flag): on the first schedule_wait() after each T0 fire,
 * read the free-running counter to measure how long dispatch took since the
 * compare match.  This overrun is subtracted from the current wait.  If the
 * wait is smaller than the overrun (the system was blocked for multiple VGM
 * periods, e.g. during a complex render frame), the excess is saved in
 * catchup_debt_ticks for stage 2.
 *
 * Stage 2 (debt drain): on every subsequent schedule_wait(), any accumulated
 * debt is subtracted from the programmed period.  When debt covers the entire
 * wait, a VGM_CATCHUP_TICKS micro-period is programmed so the OPL3 gets its
 * required bus-cycle gap before the next write group.  This preserves event
 * order and musical correctness while converging back to real time.
 *
 * Stereo micro-wait exemption: in both stages, when a wait would be collapsed
 * but the original wait is ≤ VGM_MIN_MICRO_WAIT_TICKS, the wait is honoured
 * in full via spin_wait() instead.  This keeps the L/R write-pair spacing the
 * VGM author intended, even during catch-up.
 *
 * Proportional compression floor: compressed ticks are clamped to at least
 * ticks/VGM_CATCHUP_DIVISOR (>= VGM_CATCHUP_TICKS).  This prevents stereo
 * delay effects -- such as the R-channel note triggering 1-2 ticks (10-20 ms)
 * before the L-channel note in furnace_bgm.vgm -- from collapsing below
 * audible thresholds during catch-up. */
static bool schedule_wait_catchup(uint32_t ticks)
{
    /* Stage 1: measure dispatch overrun after a real T0 fire. */
    if (vgm_flags & VGM_FLAG_COMPENSATE) {
        uint32_t now = timer_t0_read_consistent();
        uint32_t overrun = (now > vgm_last_period) ? (now - vgm_last_period) : 0u;
        bool was_catching_up = (vgm_catchup_debt_ticks > 0u);
        if (overrun >= ticks) {
            /* Overrun exceeds this entire wait; save excess as future debt. */
            vgm_catchup_debt_ticks += overrun - ticks;
            if (ticks <= VGM_MIN_MICRO_WAIT_TICKS && !was_catching_up) {
                /* Preserve micro-wait only when not already in catch-up,
                 * so we don't stall more when the player is already lagging. */
                vgm_flags &= (uint8_t)~VGM_FLAG_COMPENSATE;
                spin_wait(ticks);
                return false;
            }
            /* We are already past the intended dispatch time.  Arm for the
             * minimum catch-up gap so the next command fires immediately.
             * This matches the original f4d0c20 clamp-to-1 behaviour and
             * prevents the period from doubling to (overrun + nominal). */
            ticks = (uint32_t)VGM_CATCHUP_TICKS;
        } else {
            ticks -= overrun;
        }
        vgm_flags &= (uint8_t)~VGM_FLAG_COMPENSATE;
    }
    /* Stage 2: drain any accumulated catch-up debt from previous late fires. */
    if (vgm_catchup_debt_ticks > 0u) {
        if (vgm_catchup_debt_ticks >= ticks) {
            vgm_catchup_debt_ticks -= ticks;
        } else {
            ticks -= vgm_catchup_debt_ticks;
            vgm_catchup_debt_ticks = 0u;
        }
    }

    return schedule_wait_arm(ticks);
}

/* Schedules the next VGM wait.  Arms T0 and sets VGM_FLAG_TIMER_RUN.
 *
 * Returns true  if T0 was armed -- caller should return VGM_WAITING.
 * Returns false if the wait was serviced inline via spin_wait() (micro-wait
 * preserved for stereo separation) -- caller should continue dispatching.
 */
static bool schedule_wait(uint32_t ticks)
{
    if (__builtin_expect((vgm_flags & VGM_FLAG_COMPENSATE) == 0u &&
                         vgm_catchup_debt_ticks == 0u, 1)) {
        return schedule_wait_arm(ticks);
    }
    return schedule_wait_catchup(ticks);
}

/* -----------------------------------------------------------------------
 * VGM header parsing helpers (stack-local header buffer)
 * ----------------------------------------------------------------------- */

/* Read four LE bytes from a raw header buffer at `off`. */
static uint32_t hdr_le32(const uint8_t *hdr, uint8_t off)
{
    return (uint32_t)hdr[off]
         | ((uint32_t)hdr[off + 1u] << 8u)
         | ((uint32_t)hdr[off + 2u] << 16u)
         | ((uint32_t)hdr[off + 3u] << 24u);
}

static uint16_t hdr_le16(const uint8_t *hdr, uint8_t off)
{
    return (uint16_t)hdr[off] | ((uint16_t)hdr[off + 1u] << 8u);
}

static inline bool vgm_end_of_stream(void)
{
    return (vgm_total_samples > 0u &&
            vgm_samples_elapsed >= vgm_total_samples &&
            vgm_loop_offset == 0u);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

vgm_status_t vgm_open(vgm_read_fn read_fn, vgm_seek_fn seek_fn,
                       void *io_ctx)
{
    uint16_t n;
    uint16_t version;
    uint32_t raw_loop;
    uint32_t raw_data_ofs;

    /* Zero the player state so the caller need not pre-zero */
    vgm_flags          = 0u;
    vgm_opl_mode       = 2u;
    vgm_buf_pos        = 0u;
    vgm_buf_len        = 0u;
    vgm_wait_carry     = 0u;
    vgm_catchup_debt_ticks = 0u;
    vgm_last_period    = 0u;
    vgm_samples_elapsed = 0u;
    vgm_total_samples  = 0u;
    vgm_loop_offset    = 0u;
    vgm_data_start     = 0u;
    vgm_stream_pos     = 0u;
    vgm_read_fn_ptr    = read_fn;
    vgm_seek_fn_ptr    = seek_fn;
    vgm_io_ctx         = io_ctx;

    /* ---- Read header into the streaming buffer (reused as scratch space).
     * The stream must already be positioned at offset 0 by the caller.
     * Reading into buf[] avoids a large stack allocation on the 256-byte
     * 6502 stack. */
    n = vgm_read_fn_ptr(vgm_io_ctx, vgm_buf, 0x60u);
    vgm_stream_pos = (uint32_t)n;

    if (n < 0x40u) {
        return VGM_ERROR;
    }

    /* Validate magic "Vgm " */
    if (vgm_buf[0] != 'V' || vgm_buf[1] != 'g' ||
        vgm_buf[2] != 'm' || vgm_buf[3] != ' ') {
        return VGM_ERROR;
    }

    /* Version (LE16 at 0x08; we only need the minor comparison value) */
    version = hdr_le16(vgm_buf, 0x08u);

    /* Total sample count (LE32 at 0x18) */
    vgm_total_samples = hdr_le32(vgm_buf, 0x18u);

    /* Loop offset (LE32 at 0x1C); relative to field position 0x1C */
    raw_loop = hdr_le32(vgm_buf, 0x1Cu);
    if (raw_loop != 0u) {
        vgm_loop_offset = 0x1Cu + raw_loop;
    }

    /* Data start offset.
     * VGM 1.50+ stores a relative data offset at 0x34.
     * Earlier versions always start at 0x40. */
    if (version >= 0x150u && n >= 0x38u) {
        raw_data_ofs = hdr_le32(vgm_buf, 0x34u);
        vgm_data_start = 0x34u + raw_data_ofs;
        if (vgm_data_start < 0x40u) {
            vgm_data_start = 0x40u; /* guard against malformed files */
        }
    } else {
        vgm_data_start = 0x40u;
    }

    /* OPL mode detection.
     * VGM spec: YM3812 (OPL2) clock at header 0x50; YMF262 (OPL3) clock at
     * header 0x5C.  Both are LE32; non-zero means the chip is present.
     * n >= 0x60 guarantees we read the full 96-byte header block. */
    if (n >= 0x60u) {
        uint32_t ymf262_clock = hdr_le32(vgm_buf, 0x5Cu);
        if (ymf262_clock != 0u) {
            vgm_opl_mode = 3u;
        }
        /* YM3812-only files leave 0x5C at zero; opl_mode stays 2 */
    }

    /* Some valid OPL3 VGM files omit the YMF262 clock field but use
     * 0x5E/0x5F commands in the data stream. Detect this case and force
     * OPL3 mode so both banks are enabled. */
    if (vgm_opl_mode == 2u) {
        uint32_t scan_pos    = vgm_data_start;
        uint32_t scan_end    = scan_pos + 4096u;
        uint8_t  scan_buf[64];
        bool     opl3_used   = false;

        vgm_seek_fn_ptr(vgm_io_ctx, vgm_data_start);
        vgm_stream_pos = vgm_data_start;
        vgm_buf_len = 0u;
        vgm_buf_pos = 0u;

        while (scan_pos < scan_end) {
            uint16_t to_read = (uint16_t)(scan_end - scan_pos);
            if (to_read > sizeof(scan_buf)) {
                to_read = (uint16_t)sizeof(scan_buf);
            }
            uint16_t got = vgm_read_fn_ptr(vgm_io_ctx, scan_buf, to_read);
            if (got == 0u) break;
            for (uint16_t i = 0u; i < got; ++i) {
                if (scan_buf[i] == 0x5Eu || scan_buf[i] == 0x5Fu) {
                    opl3_used = true;
                    break;
                }
            }
            if (opl3_used) break;
            scan_pos += got;
            vgm_stream_pos += got;
        }

        if (opl3_used) {
            vgm_opl_mode = 3u;
        }
    }

    /* Initialise the OPL chip */
    opl_init(vgm_opl_mode);

    /* Seek to data and prime the buffer */
    buf_seek(vgm_data_start);

    return VGM_PLAYING;
}

/* -----------------------------------------------------------------------
 * vgm_service -- inner dispatch loop
 * ----------------------------------------------------------------------- */
__attribute__((noinline))
vgm_status_t vgm_service(void)
{
    uint8_t cmd;
    uint8_t reg, val;
    uint32_t skip32;
    bool check_end_of_data = false;

    /* Already done? */
    if (vgm_flags & VGM_FLAG_DONE) {
        return VGM_DONE;
    }

    /* ----- Timer / wait handling ----- */
    if (vgm_flags & VGM_FLAG_TIMER_RUN) {
        if (!timer_t0_is_done()) {
            return VGM_WAITING;
        }
        /* T0 fired (T0_CMP_CTR=0, no RECLEAR): the counter keeps running
         * past the compare value and cannot fire again until timer_set_period()
         * clears it with CTR_CLEAR for the next wait.  Read the current counter
         * value to credit elapsed real time to the general alarm subsystem,
         * then clear the timer-run flag. */
        timer_t0_tick_elapsed(vgm_last_period);
        vgm_flags &= (uint8_t)~VGM_FLAG_TIMER_RUN;
        vgm_flags |= VGM_FLAG_COMPENSATE;  /* arm overrun compensation */
        check_end_of_data = true;

        if (vgm_wait_carry > 0u) {
            /* Remainder of a split wait: schedule next chunk. */
            uint32_t carry_ticks = vgm_wait_carry;
            vgm_wait_carry = 0u;
            if (schedule_wait(carry_ticks)) { return VGM_WAITING; }
            /* Micro-wait done inline; fall through to dispatch. */
        }
        /* Fall through to dispatch the next command(s). */
    }

    /* ----- Command dispatch loop ----- */
    for (;;) {

        /* End-of-stream guard only after a sample-advancing wait. */
        if (check_end_of_data && vgm_end_of_stream()) {
            goto end_of_data;
        }
        check_end_of_data = false;

        /* Buffer exhaustion guard */
        if (vgm_buf_pos >= vgm_buf_len) {
            buf_refill();
            if (vgm_buf_len == 0u) {
                /* Unexpected EOF */
                goto end_of_data;
            }
        }

        cmd = vgm_buf[vgm_buf_pos++];

        switch (cmd) {

        /* ---- OPL writes ---- */
        case 0x5Au:  /* YM3812 (OPL2) register write */
        case 0x5Eu:  /* YMF262 port 0 register write */
            reg = buf_get();
            val = buf_get();
            opl_write_port0(reg, val);
            break;

        case 0x5Fu:  /* YMF262 port 1 register write */
            reg = buf_get();
            val = buf_get();
            opl_write_port1(reg, val);
            break;

        /* ---- Wait commands ---- */

        /* Variable wait: read a 16-bit sample count and convert to timer ticks.
         * 100 Hz sequences (Furnace default) always emit exactly 441 samples;
         * 200 Hz quantized sequences (vgm_quantize default) emit 220 samples;
         * special-case both to skip the hardware MULU call entirely. */
        case 0x61u: {
                uint16_t samples = buf_get_le16();
            vgm_samples_elapsed += (uint32_t)samples;
                if (schedule_wait(
                    (samples == 441u) ? TICKS_100HZ :
                    (samples == 220u) ? TICKS_200HZ :
                    samples_to_ticks(samples))) { return VGM_WAITING; }
            check_end_of_data = true;
            break; /* micro-wait done inline */
        }

        /* Fixed waits: use precomputed tick constants -- no multiply needed. */
        case 0x62u:  /* Wait 735 samples (1/60 s, NTSC frame) */
            vgm_samples_elapsed += 735u;
            if (schedule_wait(TICKS_ONE_NTSC)) { return VGM_WAITING; }
            check_end_of_data = true;
            break; /* micro-wait done inline (never triggered in practice) */

        case 0x63u:  /* Wait 882 samples (1/50 s, PAL frame) */
            vgm_samples_elapsed += 882u;
            if (schedule_wait(TICKS_ONE_PAL)) { return VGM_WAITING; }
            check_end_of_data = true;
            break; /* micro-wait done inline (never triggered in practice) */

        /* Short waits: 0x70-0x7F → wait (cmd & 0x0F)+1 samples.
         * Lookup table avoids per-call multiply. */
        case 0x70u ... 0x7Fu: {
            uint8_t s = (cmd & 0x0Fu) + 1u;
            vgm_samples_elapsed += (uint32_t)s;
            if (schedule_wait(short_wait_ticks[s])) { return VGM_WAITING; }
            check_end_of_data = true;
            break; /* micro-wait (s≤4) done inline */
        }

        /* YM2612 PCM slot: 0x80-0x8F → wait (cmd & 0x0F) samples (no OPL write).
         * Lookup table used; index 0 short-circuits so schedule_wait(0) is
         * never called. */
        case 0x80u ... 0x8Fu: {
            uint8_t s = cmd & 0x0Fu;
            if (s > 0u) {
                vgm_samples_elapsed += (uint32_t)s;
                if (schedule_wait(short_wait_ticks[s])) { return VGM_WAITING; }
                check_end_of_data = true;
                /* micro-wait (s≤4) done inline */
            }
            break;
        }

        /* ---- End of data ---- */
        case 0x66u:
end_of_data:
            if (vgm_loop_offset != 0u && !(vgm_flags & VGM_FLAG_LOOPED)) {
                /* Play the loop section once */
                vgm_flags |= VGM_FLAG_LOOPED;
                vgm_samples_elapsed = 0u;
                buf_seek(vgm_loop_offset);
                /* Continue dispatching from the loop point */
                break;
            }
            /* Done */
            vgm_flags |= VGM_FLAG_DONE;
            return VGM_DONE;

        /* ---- Data block (0x67) ---- */
        case 0x67u: {
            /* type (1) + compat (1) + size LE32 (4) -- skip the whole block */
            buf_skip(2u);                        /* type + compat bytes */
            /* Read the 4-byte block size then skip that many bytes. */
            skip32 = buf_get_le32();
            {
                uint16_t avail = (uint16_t)(vgm_buf_len - vgm_buf_pos);
                if (skip32 <= (uint32_t)avail) {
                    vgm_buf_pos += (uint8_t)skip32;
                } else {
                    /* Use stream_pos to compute the absolute target offset. */
                    uint32_t cur = vgm_stream_pos
                                 - (uint32_t)vgm_buf_len
                                 + (uint32_t)vgm_buf_pos;
                    buf_seek(cur + skip32);
                }
            }
            break;
        }

        /* ---- Skip 1-operand commands ---- */
        case 0x30u ... 0x3Fu:
        case 0x4Fu:  /* Game Gear PSG stereo, not applicable */
        case 0x50u:  /* PSG (SN76489) write, not applicable */
            buf_skip(1u);
            break;

        /* ---- Skip 2-operand commands ---- */
        case 0x40u ... 0x4Eu:
        case 0x51u ... 0x59u:
        case 0x5Bu ... 0x5Du:
        case 0xA0u:
        /* 0xB0-0xC8 */
        case 0xB0u ... 0xC8u:
            buf_skip(2u);
            break;

        /* ---- Skip 3-operand commands ---- */
        case 0xC9u ... 0xDFu:
            buf_skip(3u);
            break;

        /* ---- Skip 4-operand commands ---- */
        case 0xE0u ... 0xFFu:
            buf_skip(4u);
            break;

        default:
            /* Unknown command -- treat as no-op and keep going */
            break;

        } /* switch */

    } /* for(;;) */
}

void vgm_close(void)
{
    uint8_t i;

    /* Disable left/right DAC output on all 18 channels first.
     * C0x bits 5:4 are the L+R output-enable bits.  Clearing them gates
     * the DAC path immediately, before any envelope or TL calculation runs,
     * guaranteeing silence regardless of ADSR state or release rate.
     * opl_init() restores these to 0x30 when the player is re-opened. */
    for (i = 0u; i < 9u; ++i) {
        opl_write_port0((uint8_t)(0xC0u + i), 0x00u);
        opl_write_port1((uint8_t)(0xC0u + i), 0x00u);
    }

    /* Also max-attenuate all operator TL registers and key-off all channels
     * so the chip is in a clean state for any subsequent re-initialisation.
     * Writes to the four unused slots (0x46,0x47,0x4E,0x4F) are no-ops. */
    for (i = 0u; i <= 0x15u; ++i) {
        opl_write_port0((uint8_t)(0x40u + i), 0x3Fu);
        opl_write_port1((uint8_t)(0x40u + i), 0x3Fu);
    }
    opl_silence();

    vgm_flags = VGM_FLAG_DONE;

    /* Restore T0 to fixed-rate 24 Hz mode so general alarms resume normally */
    timer_t0_set();
}
