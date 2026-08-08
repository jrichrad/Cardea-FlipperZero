/* Cardea - the CC1101 side.
 *
 * One worker thread. It hops the key bands, camps on whichever one speaks,
 * pulls bursts out of the RSSI stream, and publishes a scored snapshot that
 * the GUI thread copies under a mutex. Nothing here decides anything -- every
 * judgement belongs to cdr_detect.c, which is testable; this file only decides
 * where the radio points.
 *
 * Receive only. Cardea never transmits.
 */
#pragma once

#include "cdr_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CdrRadio CdrRadio;

typedef enum {
    /* The main mode: hop, camp, score. */
    CdrModeGuard,
    /* Learning a key. Same machinery, but it only accepts bursts loud enough
     * to have come from a key held next to the Flipper, so the neighbour's car
     * cannot be enrolled as yours by accident. */
    CdrModeLearn,
} CdrRadioMode;

/* A key held at learning distance is enormous. Anything quieter is somebody
 * else's. */
#define CDR_LEARN_MIN_DB 25

/* ------------------------------------------------------------------ *
 * The activity strip
 * ------------------------------------------------------------------ */

/* One column per pixel of the guard view's raster, so the strip on screen is
 * the data and not a resampling of it. 122 columns at a quarter-second each is
 * a half-minute of history -- long enough to watch a poll train build, short
 * enough that it is all still relevant. */
#define CDR_RASTER_LEN 122
#define CDR_RASTER_SLOT_MS 250

/* Ceiling for a raster column, in dB over the floor. Nothing legible happens
 * above this and the display only has 26 pixels to give it. */
#define CDR_RASTER_MAX_DB 40

/* ------------------------------------------------------------------ *
 * Timing
 * ------------------------------------------------------------------ */

/* Listening time per band on a hop pass. Five bands plus retune overhead comes
 * to about a 50 ms cycle, so a single 40 ms key frame may well be missed --
 * which is fine and is the whole reason camping exists. What must not be
 * missed is a *train*, and a train cannot hide from a 50 ms cycle. */
#define CDR_HOP_DWELL_MS 8

/* Silence that ends a camp, and the hard ceiling on one. The ceiling matters:
 * without it, one chatty sensor could hold the receiver on its own band all
 * night while the attack happened two bands away. */
#define CDR_CAMP_IDLE_MS 1500u
#define CDR_CAMP_MAX_MS 8000u

/* A held band can only be measured while the receiver is sitting on it, and
 * the receiver has to leave eventually. Latching the finding for a while stops
 * the warning from strobing on and off as the hop cycle comes back round. */
#define CDR_LINK_HOLD_MS 10000u

/* ------------------------------------------------------------------ *
 * Snapshot
 * ------------------------------------------------------------------ */

typedef struct {
    /* where the radio is */
    uint8_t band;
    bool camped;
    int16_t rssi;
    uint32_t sweeps;

    /* per band */
    int16_t floor[CdrBandCount];
    int16_t peak[CdrBandCount]; /* max-hold since the session began */
    uint16_t hits[CdrBandCount]; /* bursts extracted */

    /* the reading */
    CdrVerdict verdict;
    CdrLink link;

    /* the session */
    uint32_t elapsed_ms;
    uint32_t bursts;
    uint8_t peak_score;
    uint8_t peak_level;
    uint32_t peak_at_ms;
    uint8_t peak_band;

    /* the most recent burst, for the detail page and for learning */
    CdrBurst last;
    bool have_last;
    uint32_t last_seq; /* increments per burst, so the GUI can spot new ones */

    /* the strip */
    uint8_t raster[CDR_RASTER_LEN];
    uint8_t raster_head; /* index of the newest column */

    uint16_t sample_hz; /* measured, not assumed */
    bool radio_ok;
} CdrSnapshot;

/* ------------------------------------------------------------------ *
 * API
 * ------------------------------------------------------------------ */

CdrRadio* cdr_radio_alloc(void);
void cdr_radio_free(CdrRadio* r);

/** Start the worker. @p band_mask is a bit per CdrBandId; zero is treated as
 *  all bands, because a watchdog that watches nothing is worse than useless. */
void cdr_radio_start(CdrRadio* r, CdrRadioMode mode, uint8_t band_mask, uint8_t sens);
void cdr_radio_stop(CdrRadio* r);
bool cdr_radio_running(CdrRadio* r);

void cdr_radio_get(CdrRadio* r, CdrSnapshot* out);

/** Declare whether the operator is away from the car. Drives the UNSOLICITED
 *  family and is the difference between "your key" and "someone's key". */
void cdr_radio_set_armed(CdrRadio* r, bool armed);

/** Hand the worker the learned key, or NULL to guard with the generic window. */
void cdr_radio_set_fob(CdrRadio* r, const CdrFob* fob);

void cdr_radio_set_sens(CdrRadio* r, uint8_t sens);
void cdr_radio_set_bands(CdrRadio* r, uint8_t band_mask);

/** Forget the evidence window, the peaks and the strip, but keep listening.
 *  Used when the operator arms, so the walk to the front door is not evidence
 *  against them. */
void cdr_radio_reset(CdrRadio* r);

/** Pin the receiver to one band, or pass CdrBandCount to resume hopping. */
void cdr_radio_pin(CdrRadio* r, uint8_t band);
uint8_t cdr_radio_pinned(CdrRadio* r);

#ifdef __cplusplus
}
#endif
