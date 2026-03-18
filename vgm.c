/*
 * vgm.c -- lightweight VGM streaming player for F256 / YMF262 (OPL3)
 *
 * Derived from first principles against the VGM specification:
 *   https://vgmrips.net/wiki/VGM_Specification
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
/* 44100 / 100 Hz = 441 samples.  Furnace exports 100 Hz sequences using the
 * variable-wait 0x61 command with this fixed value every tick.  Avoid the
 * hardware MULU call by detecting the common case at compile time. */
#define TICKS_100HZ     ((uint32_t)441u * VGM_TICKS_PER_SAMPLE)  /* 0x3D5EB */

/* -----------------------------------------------------------------------
 * OPL3 helpers
 * ----------------------------------------------------------------------- */

/* Write one OPL3 register.  Selects port 0 (reg < 0x100) or port 1. */
static void opl_write(uint16_t reg, uint8_t val)
{
    if (reg & 0x100u) {
        POKE(OPL_ADDR_H, (uint8_t)(reg & 0xFFu));
    } else {
        POKE(OPL_ADDR_L, (uint8_t)(reg & 0xFFu));
    }
    POKE(OPL_DATA, val);
}

/* Key-off all 18 OPL3 channels (9 per bank).
 * Uses the current key/fnum register value but clears the key-on bit.
 * This matches the behavior of the reference opl3_quietAll() implementation
 * and avoids relying on surrounding state (block/fnum) for silence. */
static void opl_silence(void)
{
    uint8_t i;
    for (i = 0u; i < 9u; ++i) {
        opl_write(0x0B0u + i, 0xDFu);  /* key-off, keep block/fnum high */
    }
    for (i = 0u; i < 9u; ++i) {
        opl_write(0x1B0u + i, 0xDFu);  /* key-off, keep block/fnum high */
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
    opl_write(0x001u, 0x20u);

    /* Disable four-operator modes */
    opl_write(0x104u, 0x00u);

    /* OPL3 enable / disable */
    opl_write(0x105u, (mode == 3u) ? 0x01u : 0x00u);

    /* Percussion/vibrato/tremolo off */
    opl_write(0x0BDu, 0x00u);

    /* Enable left+right output on all channels (both banks) */
    for (i = 0u; i < 9u; ++i) {
        opl_write(0x0C0u + i, 0x30u);
        opl_write(0x1C0u + i, 0x30u);
    }

    /* Key-off all channels */
    opl_silence();
}

/* -----------------------------------------------------------------------
 * Stream buffer helpers
 * ----------------------------------------------------------------------- */

/* Refill the stream buffer using the client read callback.  Resets buf_pos
 * and advances stream_pos past the newly-loaded bytes. */
static void buf_refill(vgm_player_t *p)
{
    uint16_t n = p->read_fn(p->io_ctx, p->buf, VGM_BUF_SIZE);
    p->buf_len = n;
    p->buf_pos = 0u;
    p->stream_pos += (uint32_t)n;
}

/* Slow path for buf_get: called only when the buffer is exhausted.
 * Marked noinline so that the single call from the inlined buf_get fast
 * path does not grow at every call site -- one copy of the refill code. */
static __attribute__((noinline)) uint8_t buf_refill_and_get(vgm_player_t *p)
{
    buf_refill(p);
    return p->buf[p->buf_pos++];
}

/* Read one byte from the stream buffer.
 * Forced inline so the compiler eliminates the JSR/RTS overhead at every
 * OPL-write call site (the most frequent hot path in the dispatch loop).
 * __builtin_expect tells the compiler the buffer-hit branch is dominant,
 * so the fast path falls through without a taken branch. */
static __attribute__((always_inline)) uint8_t buf_get(vgm_player_t *p)
{
    if (__builtin_expect(p->buf_pos < p->buf_len, 1)) {
        return p->buf[p->buf_pos++];
    }
    return buf_refill_and_get(p);
}

/* Skip `n` bytes in the stream, accounting for the in-memory buffer.
 * For skips larger than the remaining buffer bytes an absolute seek is used. */
static void buf_skip(vgm_player_t *p, uint8_t n)
{
    uint16_t avail = p->buf_len - p->buf_pos;
    if ((uint16_t)n <= avail) {
        p->buf_pos += (uint16_t)n;
    } else {
        /* Compute absolute target position using stream_pos.
         * stream_pos points one past the last byte loaded into buf[], so:
         *   abs(buf[buf_pos]) = stream_pos - buf_len + buf_pos  */
        uint32_t cur = p->stream_pos - (uint32_t)p->buf_len + (uint32_t)p->buf_pos;
        /* buf_seek handles the seek + refill */
        p->seek_fn(p->io_ctx, cur + (uint32_t)n);
        p->stream_pos = cur + (uint32_t)n;
        p->buf_len = 0u;
        p->buf_pos = 0u;
        buf_refill(p);
    }
}

/* Seek the stream to an absolute offset and refill the buffer. */
static void buf_seek(vgm_player_t *p, uint32_t offset)
{
    p->seek_fn(p->io_ctx, offset);
    p->stream_pos = offset;
    p->buf_len    = 0u;
    p->buf_pos    = 0u;
    buf_refill(p);
}

/* -----------------------------------------------------------------------
 * Wait / timer helpers
 * ----------------------------------------------------------------------- */

/* Read LE16 from the stream (2 buf_get calls). */
static uint16_t buf_get_le16(vgm_player_t *p)
{
    uint8_t lo = buf_get(p);
    uint8_t hi = buf_get(p);
    return (uint16_t)lo | ((uint16_t)hi << 8u);
}

/* Read LE32 from the stream (4 buf_get calls). */
static uint32_t buf_get_le32(vgm_player_t *p)
{
    uint32_t v  = (uint32_t)buf_get(p);
    v |= (uint32_t)buf_get(p) << 8u;
    v |= (uint32_t)buf_get(p) << 16u;
    v |= (uint32_t)buf_get(p) << 24u;
    return v;
}

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

static void schedule_wait(vgm_player_t *p, uint32_t ticks)
{
    /* Overrun compensation: T0 continues counting after its compare match
     * fires (one-shot mode, no RECLEAR).  By the time schedule_wait() is
     * called the counter reads last_period + overrun, where overrun is the
     * wall-clock time consumed by OPL-register write wait-states and other
     * dispatch overhead.  Subtracting that overrun from the next tick period
     * keeps each inter-match interval accurate.
     *
     * The flag is set by vgm_service() on every T0 fire and cleared here so
     * that only the first schedule_wait() after each fire is compensated;
     * subsequent carry sub-waits (which do not involve OPL dispatching) are
     * compensated solely by their own compare-match/fire cycle. */
    if (p->flags & VGM_FLAG_COMPENSATE) {
        uint32_t now = readTimer0_consistent();
        uint32_t overrun = (now > p->last_period) ? (now - p->last_period) : 0u;
        ticks = (ticks > overrun) ? ticks - overrun : 1u;
        p->flags &= (uint8_t)~VGM_FLAG_COMPENSATE;
    }
    if (ticks > VGM_MAX_WAIT_TICKS) {
        p->wait_carry = ticks - VGM_MAX_WAIT_TICKS;
        ticks = VGM_MAX_WAIT_TICKS;
    } else {
        p->wait_carry = 0u;
    }
    p->last_period = ticks;
    timer_set_period(ticks);
    p->flags |= VGM_FLAG_TIMER_RUN;
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

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

vgm_status_t vgm_open(vgm_player_t *p,
                       vgm_read_fn read_fn, vgm_seek_fn seek_fn,
                       void *io_ctx)
{
    uint16_t n;
    uint16_t version;
    uint32_t raw_loop;
    uint32_t raw_data_ofs;

    /* Zero the player state so the caller need not pre-zero */
    p->flags          = 0u;
    p->opl_mode       = 2u;
    p->buf_pos        = 0u;
    p->buf_len        = 0u;
    p->wait_carry     = 0u;
    p->last_period    = 0u;
    p->samples_elapsed = 0u;
    p->total_samples  = 0u;
    p->loop_offset    = 0u;
    p->data_start     = 0u;
    p->stream_pos     = 0u;
    p->read_fn        = read_fn;
    p->seek_fn        = seek_fn;
    p->io_ctx         = io_ctx;

    /* ---- Read header into the streaming buffer (reused as scratch space).
     * The stream must already be positioned at offset 0 by the caller.
     * Reading into buf[] avoids a large stack allocation on the 256-byte
     * 6502 stack. */
    n = read_fn(io_ctx, p->buf, 0x60u);
    p->stream_pos = (uint32_t)n;

    if (n < 0x40u) {
        return VGM_ERROR;
    }

    /* Validate magic "Vgm " */
    if (p->buf[0] != 'V' || p->buf[1] != 'g' ||
        p->buf[2] != 'm' || p->buf[3] != ' ') {
        return VGM_ERROR;
    }

    /* Version (LE16 at 0x08; we only need the minor comparison value) */
    version = hdr_le16(p->buf, 0x08u);

    /* Total sample count (LE32 at 0x18) */
    p->total_samples = hdr_le32(p->buf, 0x18u);

    /* Loop offset (LE32 at 0x1C); relative to field position 0x1C */
    raw_loop = hdr_le32(p->buf, 0x1Cu);
    if (raw_loop != 0u) {
        p->loop_offset = 0x1Cu + raw_loop;
    }

    /* Data start offset.
     * VGM 1.50+ stores a relative data offset at 0x34.
     * Earlier versions always start at 0x40. */
    if (version >= 0x150u && n >= 0x38u) {
        raw_data_ofs = hdr_le32(p->buf, 0x34u);
        p->data_start = 0x34u + raw_data_ofs;
        if (p->data_start < 0x40u) {
            p->data_start = 0x40u; /* guard against malformed files */
        }
    } else {
        p->data_start = 0x40u;
    }

    /* OPL mode detection.
     * YM3812 (OPL2) clock at 0x50; YMF262 (OPL3) clock at 0x54.
     * Both are LE32; non-zero means the chip is present. */
    if (n >= 0x58u) {
        uint32_t ymf262_clock = hdr_le32(p->buf, 0x54u);
        if (ymf262_clock != 0u) {
            p->opl_mode = 3u;
        }
        /* YM3812-only files leave 0x54 at zero; opl_mode stays 2 */
    }

    /* Initialise the OPL chip */
    opl_init(p->opl_mode);

    /* Seek to data and prime the buffer */
    buf_seek(p, p->data_start);

    return VGM_PLAYING;
}

/* -----------------------------------------------------------------------
 * vgm_service -- inner dispatch loop
 * ----------------------------------------------------------------------- */

vgm_status_t vgm_service(vgm_player_t *p)
{
    uint8_t cmd;
    uint8_t reg, val;
    uint32_t skip32;

    /* Already done? */
    if (p->flags & VGM_FLAG_DONE) {
        return VGM_DONE;
    }

    /* ----- Timer / wait handling ----- */
    if (p->flags & VGM_FLAG_TIMER_RUN) {
        if (!isTimerDone()) {
            return VGM_WAITING;
        }
        /* T0 fired (T0_CMP_CTR=0, no RECLEAR): the counter keeps running
         * past the compare value and cannot fire again until timer_set_period()
         * clears it with CTR_CLEAR for the next wait.  Clear the pending flag
         * and service any elapsed alarms. */
        timer_tick_elapsed(p->last_period);
        p->flags &= (uint8_t)~VGM_FLAG_TIMER_RUN;
        p->flags |= VGM_FLAG_COMPENSATE;  /* arm overrun compensation */

        if (p->wait_carry > 0u) {
            /* More time to wait; schedule the remainder */
            schedule_wait(p, p->wait_carry);
            return VGM_WAITING;
        }
        /* Fall through to dispatch the next command(s). */
    }

    /* ----- Command dispatch loop ----- */
    for (;;) {

        /* End-of-stream guard via sample count */
        if (p->total_samples > 0u &&
            p->samples_elapsed >= p->total_samples &&
            p->loop_offset == 0u) {
            goto end_of_data;
        }

        /* Buffer exhaustion guard */
        if (p->buf_pos >= p->buf_len) {
            buf_refill(p);
            if (p->buf_len == 0u) {
                /* Unexpected EOF */
                goto end_of_data;
            }
        }

        cmd = p->buf[p->buf_pos++];

        switch (cmd) {

        /* ---- OPL writes ---- */
        case 0x5Au:  /* YM3812 (OPL2) register write */
        case 0x5Eu:  /* YMF262 port 0 register write */
            reg = buf_get(p);
            val = buf_get(p);
            opl_write((uint16_t)reg, val);
            break;

        case 0x5Fu:  /* YMF262 port 1 register write */
            reg = buf_get(p);
            val = buf_get(p);
            opl_write(0x100u | (uint16_t)reg, val);
            break;

        /* ---- Wait commands ---- */

        /* Variable wait: read a 16-bit sample count and convert to timer ticks.
         * 100 Hz sequences (Furnace default) always emit exactly 441 samples;
         * special-case it to skip the hardware MULU call entirely. */
        case 0x61u: {
            uint16_t samples = buf_get_le16(p);
            p->samples_elapsed += (uint32_t)samples;
            schedule_wait(p,
                (samples == 441u) ? TICKS_100HZ : samples_to_ticks(samples));
            return VGM_WAITING;
        }

        /* Fixed waits: use precomputed tick constants -- no multiply needed. */
        case 0x62u:  /* Wait 735 samples (1/60 s, NTSC frame) */
            p->samples_elapsed += 735u;
            schedule_wait(p, TICKS_ONE_NTSC);
            return VGM_WAITING;

        case 0x63u:  /* Wait 882 samples (1/50 s, PAL frame) */
            p->samples_elapsed += 882u;
            schedule_wait(p, TICKS_ONE_PAL);
            return VGM_WAITING;

        /* Short waits: 0x70-0x7F → wait (cmd & 0x0F)+1 samples.
         * Lookup table avoids per-call multiply. */
        case 0x70u: case 0x71u: case 0x72u: case 0x73u:
        case 0x74u: case 0x75u: case 0x76u: case 0x77u:
        case 0x78u: case 0x79u: case 0x7Au: case 0x7Bu:
        case 0x7Cu: case 0x7Du: case 0x7Eu: case 0x7Fu: {
            uint8_t s = (cmd & 0x0Fu) + 1u;
            p->samples_elapsed += (uint32_t)s;
            schedule_wait(p, short_wait_ticks[s]);
            return VGM_WAITING;
        }

        /* YM2612 PCM slot: 0x80-0x8F → wait (cmd & 0x0F) samples (no OPL write).
         * Lookup table used; index 0 short-circuits so schedule_wait(0) is
         * never called. */
        case 0x80u: case 0x81u: case 0x82u: case 0x83u:
        case 0x84u: case 0x85u: case 0x86u: case 0x87u:
        case 0x88u: case 0x89u: case 0x8Au: case 0x8Bu:
        case 0x8Cu: case 0x8Du: case 0x8Eu: case 0x8Fu: {
            uint8_t s = cmd & 0x0Fu;
            if (s > 0u) {
                p->samples_elapsed += (uint32_t)s;
                schedule_wait(p, short_wait_ticks[s]);
                return VGM_WAITING;
            }
            break;
        }

        /* ---- End of data ---- */
        case 0x66u:
end_of_data:
            if (p->loop_offset != 0u && !(p->flags & VGM_FLAG_LOOPED)) {
                /* Play the loop section once */
                p->flags |= VGM_FLAG_LOOPED;
                p->samples_elapsed = 0u;
                buf_seek(p, p->loop_offset);
                /* Continue dispatching from the loop point */
                break;
            }
            /* Done */
            p->flags |= VGM_FLAG_DONE;
            return VGM_DONE;

        /* ---- Data block (0x67) ---- */
        case 0x67u: {
            /* type (1) + compat (1) + size LE32 (4) -- skip the whole block */
            buf_skip(p, 2u);                        /* type + compat bytes */
            /* Read the 4-byte block size then skip that many bytes. */
            skip32 = buf_get_le32(p);
            {
                uint16_t avail = p->buf_len - p->buf_pos;
                if (skip32 <= (uint32_t)avail) {
                    p->buf_pos += (uint16_t)skip32;
                } else {
                    /* Use stream_pos to compute the absolute target offset. */
                    uint32_t cur = p->stream_pos
                                 - (uint32_t)p->buf_len
                                 + (uint32_t)p->buf_pos;
                    buf_seek(p, cur + skip32);
                }
            }
            break;
        }

        /* ---- Skip 1-operand commands ---- */
        case 0x30u: case 0x31u: case 0x32u: case 0x33u:
        case 0x34u: case 0x35u: case 0x36u: case 0x37u:
        case 0x38u: case 0x39u: case 0x3Au: case 0x3Bu:
        case 0x3Cu: case 0x3Du: case 0x3Eu: case 0x3Fu:
        case 0x4Fu:  /* Game Gear PSG stereo, not applicable */
        case 0x50u:  /* PSG (SN76489) write, not applicable */
            buf_skip(p, 1u);
            break;

        /* ---- Skip 2-operand commands ---- */
        case 0x40u: case 0x41u: case 0x42u: case 0x43u:
        case 0x44u: case 0x45u: case 0x46u: case 0x47u:
        case 0x48u: case 0x49u: case 0x4Au: case 0x4Bu:
        case 0x4Cu: case 0x4Du: case 0x4Eu:
        case 0x51u: case 0x52u: case 0x53u: case 0x54u:
        case 0x55u: case 0x56u: case 0x57u: case 0x58u:
        case 0x59u:
        case 0x5Bu: case 0x5Cu: case 0x5Du:
        case 0xA0u:
        /* 0xB0-0xC8 */
        case 0xB0u: case 0xB1u: case 0xB2u: case 0xB3u:
        case 0xB4u: case 0xB5u: case 0xB6u: case 0xB7u:
        case 0xB8u: case 0xB9u: case 0xBAu: case 0xBBu:
        case 0xBCu: case 0xBDu: case 0xBEu: case 0xBFu:
        case 0xC0u: case 0xC1u: case 0xC2u: case 0xC3u:
        case 0xC4u: case 0xC5u: case 0xC6u: case 0xC7u:
        case 0xC8u:
            buf_skip(p, 2u);
            break;

        /* ---- Skip 3-operand commands ---- */
        case 0xC9u: case 0xCAu: case 0xCBu: case 0xCCu:
        case 0xCDu: case 0xCEu: case 0xCFu:
        case 0xD0u: case 0xD1u: case 0xD2u: case 0xD3u:
        case 0xD4u: case 0xD5u: case 0xD6u: case 0xD7u:
        case 0xD8u: case 0xD9u: case 0xDAu: case 0xDBu:
        case 0xDCu: case 0xDDu: case 0xDEu: case 0xDFu:
            buf_skip(p, 3u);
            break;

        /* ---- Skip 4-operand commands ---- */
        case 0xE0u: case 0xE1u: case 0xE2u: case 0xE3u:
        case 0xE4u: case 0xE5u: case 0xE6u: case 0xE7u:
        case 0xE8u: case 0xE9u: case 0xEAu: case 0xEBu:
        case 0xECu: case 0xEDu: case 0xEEu: case 0xEFu:
        case 0xF0u: case 0xF1u: case 0xF2u: case 0xF3u:
        case 0xF4u: case 0xF5u: case 0xF6u: case 0xF7u:
        case 0xF8u: case 0xF9u: case 0xFAu: case 0xFBu:
        case 0xFCu: case 0xFDu: case 0xFEu: case 0xFFu:
            buf_skip(p, 4u);
            break;

        default:
            /* Unknown command -- treat as no-op and keep going */
            break;

        } /* switch */

    } /* for(;;) */
}

void vgm_close(vgm_player_t *p)
{
    uint8_t i;

    /* Disable left/right DAC output on all 18 channels first.
     * C0x bits 5:4 are the L+R output-enable bits.  Clearing them gates
     * the DAC path immediately, before any envelope or TL calculation runs,
     * guaranteeing silence regardless of ADSR state or release rate.
     * opl_init() restores these to 0x30 when the player is re-opened. */
    for (i = 0u; i < 9u; ++i) {
        opl_write(0x0C0u + i, 0x00u);
        opl_write(0x1C0u + i, 0x00u);
    }

    /* Also max-attenuate all operator TL registers and key-off all channels
     * so the chip is in a clean state for any subsequent re-initialisation.
     * Writes to the four unused slots (0x46,0x47,0x4E,0x4F) are no-ops. */
    for (i = 0u; i <= 0x15u; ++i) {
        opl_write(0x040u + (uint16_t)i, 0x3Fu);
        opl_write(0x140u + (uint16_t)i, 0x3Fu);
    }
    opl_silence();

    p->flags = VGM_FLAG_DONE;

    /* Restore T0 to fixed-rate 24 Hz mode so general alarms resume normally */
    setTimer0();
}
