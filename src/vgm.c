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

#include "../include/opl3_io.h"
#include "../include/timer.h"
#include "../include/vgm.h"


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
 * 25,175,000 Hz.  Ticks per VGM sample = 25175000 / 44100 ~ 571
 * ----------------------------------------------------------------------- */
/* VGM_TICKS_PER_SAMPLE is defined in include/vgm.h (shared with vgm_fx.c) */

/* Maximum value that fits in T0's 24-bit compare register */
#define T0_MAX_TICKS 0x00FFFFFFu

/* Precomputed tick counts for the two most common wait commands */

#define TICKS_ONE_NTSC  ((uint32_t)735u * VGM_TICKS_PER_SAMPLE)  /* 0x066525 */
#define TICKS_ONE_PAL   ((uint32_t)882u * VGM_TICKS_PER_SAMPLE)  /* 0x07AEC6 */
/* Special Case 0x61 variable timing values to avoid a mult */
#define TICKS_100HZ     ((uint32_t)441u * VGM_TICKS_PER_SAMPLE)  /* 0x3D5EB */
#define TICKS_200HZ     ((uint32_t)220u * VGM_TICKS_PER_SAMPLE)  /* 0x1EAF4 */

/* -----------------------------------------------------------------------
 * OPL3 helpers (register write primitives defined in opl3_io.h)
 * ----------------------------------------------------------------------- */

/* Busy-wait for small 'micro-waits'.  Used during catch-up processing 
 * to preserve OPL3 stereo L/R write-pair spacing.
 */
static void spin_wait(uint32_t ticks)
{
    uint32_t start = timer_t0_read_consistent();
    while (((timer_t0_read_consistent() - start) & T0_MASK_TICKS) < ticks) {
        /* busy wait */
    }
}

/* Key-off all 18 OPL3 channels (9 per bank).
 */
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
 * is serviced at least once per frame even during long VGM silence or pause commands.
 */
#define VGM_MAX_WAIT_TICKS  T0_TICK_PERIOD_TICKS

#define VGM_CATCHUP_TICKS 1024u


/* Micro-waits at or below this threshold are honored in full (by busy-wait)
 * even during catch-up. */
#define VGM_MIN_MICRO_WAIT_TICKS ((uint32_t)4u * VGM_TICKS_PER_SAMPLE)

static __attribute__((always_inline)) bool schedule_wait_arm(uint32_t ticks)
{
    /* Frame-cap: split waits longer than one frame */
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

static bool schedule_wait_catchup(uint32_t ticks)
{
    /* Measure dispatch overrun after a real T0 fire. */
    if (vgm_flags & VGM_FLAG_COMPENSATE) {
        uint32_t now = timer_t0_read_consistent();
        uint32_t overrun = (now > vgm_last_period) ? (now - vgm_last_period) : 0u;
        bool was_catching_up = (vgm_catchup_debt_ticks > 0u);
        if (overrun >= ticks) {
            /* Overrun exceeds this entire wait; save excess as future debt. */
            vgm_catchup_debt_ticks += overrun - ticks;
            if (ticks <= VGM_MIN_MICRO_WAIT_TICKS && !was_catching_up) {
                /* only spin if not already in catching up wait */
                vgm_flags &= (uint8_t)~VGM_FLAG_COMPENSATE;
                spin_wait(ticks);
                return false;
            }
            /* We are already past the intended dispatch time.  Arm for the
             * minimum catch-up gap so the next command fires immediately.
             */
            ticks = (uint32_t)VGM_CATCHUP_TICKS;
        } else {
            ticks -= overrun;
        }
        vgm_flags &= (uint8_t)~VGM_FLAG_COMPENSATE;
    }
    /* Drain any accumulated catch-up debt from previous late fires. */
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

/* Schedules the next VGM wait.  Arms T0 and sets VGM_FLAG_TIMER_RUN. */
static bool schedule_wait(uint32_t ticks)
{
    if (__builtin_expect((vgm_flags & VGM_FLAG_COMPENSATE) == 0u &&
                         vgm_catchup_debt_ticks == 0u, 1)) {
        return schedule_wait_arm(ticks);
    }
    return schedule_wait_catchup(ticks);
}

/* -----------------------------------------------------------------------
 * VGM header parsing helpers
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
 * API
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
    /* Channel shadows removed; hardware initialisation already sets C0=0x30. */
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

    /* Data start offset. */
    if (version >= 0x150u && n >= 0x38u) {
        raw_data_ofs = hdr_le32(vgm_buf, 0x34u);
        vgm_data_start = 0x34u + raw_data_ofs;
        if (vgm_data_start < 0x40u) {
            vgm_data_start = 0x40u; /* guard against malformed files */
        }
    } else {
        vgm_data_start = 0x40u;
    }

    /* OPL mode detection. */
    if (n >= 0x60u) {
        uint32_t ymf262_clock = hdr_le32(vgm_buf, 0x5Cu);
        if (ymf262_clock != 0u) {
            vgm_opl_mode = 3u;
        }
    }

    /* Additional OPL3 detection */
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
    }

    /* ----- Command dispatch loop ----- */
    for (;;) {

        if (check_end_of_data && vgm_end_of_stream()) {
            goto end_of_data;
        }
        check_end_of_data = false;

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
         * 100 Hz sequences always emit exactly 441 samples;
         * 200 Hz quantized sequences emit 220 samples;
         * special-case both to skip the hardware mult call entirely. */
        case 0x61u: {
                uint16_t samples = buf_get_le16();
            vgm_samples_elapsed += (uint32_t)samples;
                if (schedule_wait(
                    (samples == 441u) ? TICKS_100HZ :
                    (samples == 220u) ? TICKS_200HZ :
                    samples_to_ticks(samples))) { return VGM_WAITING; }
            check_end_of_data = true;
            break; 
        }

        /* Fixed waits */
        case 0x62u:  /* Wait 735 samples (1/60 s, NTSC frame) */
            vgm_samples_elapsed += 735u;
            if (schedule_wait(TICKS_ONE_NTSC)) { return VGM_WAITING; }
            check_end_of_data = true;
            break; 

        case 0x63u:  /* Wait 882 samples (1/50 s, PAL frame) */
            vgm_samples_elapsed += 882u;
            if (schedule_wait(TICKS_ONE_PAL)) { return VGM_WAITING; }
            check_end_of_data = true;
            break;

        /* Short waits: 0x70-0x7F → wait (cmd & 0x0F)+1 samples.
         * Lookup table avoids per-call multiply. */
        case 0x70u ... 0x7Fu: {
            uint8_t s = (cmd & 0x0Fu) + 1u;
            vgm_samples_elapsed += (uint32_t)s;
            if (schedule_wait(short_wait_ticks[s])) { return VGM_WAITING; }
            check_end_of_data = true;
            break;
        }

        case 0x80u ... 0x8Fu: {
            uint8_t s = cmd & 0x0Fu;
            if (s > 0u) {
                vgm_samples_elapsed += (uint32_t)s;
                if (schedule_wait(short_wait_ticks[s])) { return VGM_WAITING; }
                check_end_of_data = true;
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
                break;
            }
            /* Done */
            vgm_flags |= VGM_FLAG_DONE;
            return VGM_DONE;

        case 0x67u: {
            buf_skip(2u);                        /* type + compat bytes */
            skip32 = buf_get_le32();
            {
                uint16_t avail = (uint16_t)(vgm_buf_len - vgm_buf_pos);
                if (skip32 <= (uint32_t)avail) {
                    vgm_buf_pos += (uint8_t)skip32;
                } else {
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
        case 0x4Fu:  
        case 0x50u: 
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

    } 
}

void vgm_close(void)
{
    uint8_t i;

    for (i = 0u; i < 9u; ++i) {
        opl_write_port0((uint8_t)(0xC0u + i), 0x00u);
        opl_write_port1((uint8_t)(0xC0u + i), 0x00u);
    }

    for (i = 0u; i <= 0x15u; ++i) {
        opl_write_port0((uint8_t)(0x40u + i), 0x3Fu);
        opl_write_port1((uint8_t)(0x40u + i), 0x3Fu);
    }
    opl_silence();

    vgm_flags = VGM_FLAG_DONE;

    /* Restore T0 to fixed-rate 24 Hz mode so general alarms resume normally */
    timer_t0_set();
}


void vgm_opl_init(void)
{
    opl_init(3u);   /* always enable OPL3 mode for FX */
}

typedef struct {
    const uint8_t *data;
    uint32_t       len;
    uint32_t       pos;
} vgm_mem_ctx_t;

static vgm_mem_ctx_t s_vgm_mem_ctx;

static uint16_t vgm_mem_read(void *ctx, uint8_t *buf, uint16_t n)
{
    vgm_mem_ctx_t *m = (vgm_mem_ctx_t *)ctx;
    uint32_t avail = (m->pos < m->len) ? (m->len - m->pos) : 0u;
    if ((uint32_t)n > avail) { n = (uint16_t)avail; }
    if (n > 0u) {
        uint16_t i;
        for (i = 0u; i < n; ++i) { buf[i] = m->data[m->pos + i]; }
        m->pos += (uint32_t)n;
    }
    return n;
}

static void vgm_mem_seek(void *ctx, uint32_t offset)
{
    vgm_mem_ctx_t *m = (vgm_mem_ctx_t *)ctx;
    m->pos = (offset < m->len) ? offset : m->len;
}

vgm_status_t vgm_open_mem(const uint8_t *data, uint32_t len)
{
    s_vgm_mem_ctx.data = data;
    s_vgm_mem_ctx.len  = len;
    s_vgm_mem_ctx.pos  = 0u;
    return vgm_open(vgm_mem_read, vgm_mem_seek, &s_vgm_mem_ctx);
}

