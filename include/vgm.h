#if !defined(INCLUDE_VGM_H__)
#define INCLUDE_VGM_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * vgm.h -- lightweight VGM playback library for the F256 / 65816 (6502 mode)
 *
 * Usage:
 *   1. Implement vgm_read_fn and vgm_seek_fn callbacks backed by whatever
 *      storage is available (SD-card file, high RAM, etc.).
 *   2. Call vgm_open(), passing your callbacks and an opaque context pointer.
 *   3. Call vgm_service() from your main loop as fast as possible.
 *   4. Optionally call timer_t0_service() in the same loop for general alarms;
 *      the shared T0 protocol ensures they coexist safely.
 *   5. vgm_service() returns VGM_DONE when playback is complete.
 *   6. Call vgm_close() to silence the chip and restore fixed-rate T0 mode.
 *      The library does NOT close the underlying stream; that is the
 *      caller's responsibility.
 *
 * Constraints and design notes:
 *   - Streams from the source; never loads the whole VGM into RAM.
 *   - VGM_BUF_SIZE bytes are refilled via read_fn as needed
 *   - Only OPL2 (0x5A) and OPL3 (0x5E / 0x5F) register-write commands are
 *     acted on.  All other command bytes are skipped per the VGM spec.
 *   - Waiting is done by programming T0 via timer_set_period() (variable-rate
 *     mode); timer_t0_service() remains callable for concurrent general alarms.
 *   - Loop point is honored once; the stream then plays to end and stops.
 *   - The library manages playback state internally; callers only implement
 *     the `vgm_read_fn`/`vgm_seek_fn` callbacks and call `vgm_open()`,
 *     `vgm_service()`, and `vgm_close()`.
 */

/* Streaming buffer size.  */
#define VGM_BUF_SIZE 255u /* to fit within 8-bit index.  MUST be 96 or larger */

/* Internal flags bits stored in the player's internal flags */
#define VGM_FLAG_LOOPED          0x01u  /* loop point has been passed once */
#define VGM_FLAG_TIMER_RUN       0x02u  /* T0 is counting a VGM wait period */
#define VGM_FLAG_DONE            0x04u  /* playback complete */
#define VGM_FLAG_COMPENSATE      0x08u  /* next schedule_wait() should subtract
                                         * T0 overrun from the programmed period */

typedef enum {
    VGM_PLAYING = 0,  /* actively dispatching commands */
    VGM_WAITING,      /* inside a wait interval; T0 not yet expired */
    VGM_DONE,         /* end of stream; call vgm_close() */
    VGM_ERROR         /* header / stream error from vgm_open() */
} vgm_status_t;

/*
 * vgm_read_fn -- sequential read callback.
 *   ctx -- user-supplied context (e.g. FILE*, high-RAM bank descriptor).
 *   buf -- destination buffer.
 *   len -- number of bytes requested.
 *   Returns the number of bytes actually placed in buf (0 = EOF / error).
 */
typedef uint16_t (*vgm_read_fn)(void *ctx, uint8_t *buf, uint16_t len);

/*
 * vgm_seek_fn -- absolute-seek callback.
 *   ctx    -- user-supplied context.
 *   offset -- byte offset from the start of the VGM stream to seek to.
 *   Seeking is always absolute; the library tracks stream_pos internally.
 */
typedef void (*vgm_seek_fn)(void *ctx, uint32_t offset);

/* Playback state is internal to the library; callers do not allocate or
 * manage a player object.  Implement the `vgm_read_fn`/`vgm_seek_fn`
 * callbacks and call `vgm_open()`, `vgm_service()`, and `vgm_close()`.
 */

/*
 * vgm_open(p, read_fn, seek_fn, io_ctx)
 *   Parse the VGM header, detect OPL2/3 mode, initialise the OPL3 chip,
 *   seek to the data block, and prime the stream buffer.
 *
 *   The stream must be positioned at byte offset 0 before this call.
 *   seek_fn MUST be non-NULL; it is called at least once during open and
 *   again whenever the loop point is revisited.
 *
 *   Returns VGM_PLAYING on success, VGM_ERROR on failure.
 */
vgm_status_t vgm_open(vgm_read_fn read_fn, vgm_seek_fn seek_fn,
                       void *io_ctx);

/*
 * vgm_service()
 *   Advance playback.  Must be called repeatedly from the main loop.
 *
 *   Returns:
 *     VGM_PLAYING  -- register writes dispatched; call again immediately.
 *     VGM_WAITING  -- inside a timed wait; call again (will return quickly
 *                     until T0 expires, then resumes dispatching).
 *     VGM_DONE     -- end of stream; call vgm_close().
 *     VGM_ERROR    -- stream read error; call vgm_close().
 */
vgm_status_t vgm_service(void);

/*
 * vgm_close()
 *   Silence all OPL3 channels and restore T0 to fixed-rate (30 Hz) mode
 *   so that general alarms resume normal operation.
 *   Safe to call from any state, including after VGM_ERROR.
 *   Does NOT close or release the underlying stream; the caller must do
 *   that via io_ctx after this function returns.
 */
void vgm_close(void);

#endif /* INCLUDE_VGM_H__ */
