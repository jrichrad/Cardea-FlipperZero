/* Cardea - relay attack evidence engine.
 *
 * Everything in this file is pure C: no Flipper headers, no radio, no clock of
 * its own. It is fed timestamped RSSI samples and hands back bursts, and it is
 * fed bursts and hands back a graded verdict. That is deliberate -- the reading
 * of the signal is the whole product, and it is the one part a screenshot
 * cannot vouch for, so it is the part that gets host tests.
 *
 * ---------------------------------------------------------------------------
 * What a relay attack looks like from the car
 * ---------------------------------------------------------------------------
 * A passive keyless entry car challenges the key over a 125 kHz magnetic field
 * that reaches about a metre. The key answers over UHF -- 315, 433 or 868 MHz
 * -- and that answer is a real radio transmission built to carry the length of
 * a car park.
 *
 * The cheap, common attack relays only the 125 kHz half: one thief holds a loop
 * at the car door to pick up the challenge, a second carries it to the key
 * indoors and re-radiates it there. Nobody relays the answer, because nobody
 * has to. The key's UHF reply is strong enough to reach the car through the
 * house wall on its own. That is exactly why the attack works with two cheap
 * boxes -- and it is exactly what leaves the attack audible from the driver's
 * seat.
 *
 * So Cardea sits with the car and listens for a key answering when no key
 * should be answering. It cannot hear the 125 kHz half; nothing on a Flipper
 * can, at that range. It does not need to.
 *
 * ---------------------------------------------------------------------------
 * Three families of evidence
 * ---------------------------------------------------------------------------
 * Each is scored separately, and no single one can reach the top verdict:
 *
 *   UNSOLICITED  a key-shaped burst while you have declared yourself away from
 *                the car. One burst is never evidence -- radios are noisy and
 *                a neighbour also owns a key. A train of them is.
 *   CADENCE      a person pressing a button produces ragged gaps. A relay
 *                poller produces a metronome. Measured as the mean absolute
 *                deviation of the gaps over their mean.
 *   CLONE        the same transmission arriving again and again with the same
 *                length, the same envelope and the same strength.
 *
 * The scorer needs two of the three before it will say RELAY LIKELY, and it
 * never says anything stronger than LIKELY, because without decrypting the
 * key's reply there is no way to prove the burst came from *your* key.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No reading. Matches the convention the rest of the family uses. */
#define CDR_DBM_INVALID (-127)

/* ------------------------------------------------------------------ *
 * Bands
 * ------------------------------------------------------------------ */

/* Every band a car key is allowed to answer on, worldwide. The CC1101 can only
 * be in one place at a time, so Guard hops these and camps on whichever one
 * speaks (see cdr_radio.c). */
typedef enum {
    CdrBand315, /* North America, Japan */
    CdrBand390, /* North America, some GM/Ford */
    CdrBand433, /* Europe, India, most of Asia, Australia */
    CdrBand868, /* Newer European vehicles */
    CdrBand915, /* Australia/NZ telemetry, a few keys */
    CdrBandCount,
} CdrBandId;

typedef struct {
    uint32_t freq;
    const char* label; /* "433.92" - fits the header, where space is scarce */
    const char* menu; /* "433.92 MHz" - for the settings list, where it is not */
    const char* where; /* "EU / IN / AU" - one line of context */
} CdrBandInfo;

extern const CdrBandInfo cdr_bands[CdrBandCount];

/* ------------------------------------------------------------------ *
 * Noise floor
 * ------------------------------------------------------------------ */

/* Min-tracking with a slow upward leak, one per band.
 *
 * The trap, learned the hard way in Vulpes: a floor that keeps tracking while
 * a strong carrier is present will walk itself up to meet that carrier, the
 * margin collapses, and the detector goes quiet while sitting on the very
 * thing it is meant to catch. So the floor stops updating once samples are
 * running CDR_FLOOR_FREEZE_DB above it, and only resumes when the band goes
 * quiet again. */
#define CDR_FLOOR_FREEZE_DB 10
#define CDR_FLOOR_LEAK_TICKS 64 /* samples between +1 dB relaxations */

typedef struct {
    int16_t floor;
    uint16_t leak;
    bool seeded;
    bool frozen;
} CdrFloor;

void cdr_floor_init(CdrFloor* f, int16_t seed);
void cdr_floor_push(CdrFloor* f, int16_t dbm);

/* ------------------------------------------------------------------ *
 * Bursts
 * ------------------------------------------------------------------ */

/* The envelope is what lets CLONE tell "the same transmission again" from
 * "another transmission of similar length". Eight bins is coarse on purpose:
 * enough to separate a flat carrier blip from a keyed frame, not so fine that
 * ordinary fading between two receptions of the same key breaks the match. */
#define CDR_ENV_BINS 8

/* Raw margin samples held while a burst is open, before binning. A burst
 * longer than this decimates in place rather than truncating, so the envelope
 * still describes the whole burst. */
#define CDR_ENV_RAW 128

typedef struct {
    uint32_t t_ms; /* start, on the guard session's clock */
    uint16_t dur_ms;
    int16_t peak_dbm;
    int16_t floor_dbm; /* the floor this burst stood above */
    uint8_t band;
    uint8_t env[CDR_ENV_BINS]; /* 0..255, normalised so the peak bin is 255 */
} CdrBurst;

/* Hysteresis. A burst opens at floor+ENTER and does not close until the signal
 * has been below floor+EXIT for TAIL_MS, so one deep fade inside a frame does
 * not get counted as two bursts. */
#define CDR_BURST_ENTER_DB 8
#define CDR_BURST_EXIT_DB 5
#define CDR_BURST_TAIL_MS 6

/* Anything shorter is a sample-rate artefact; anything longer is not a key
 * frame, it is a carrier, and the link detector deals with those. */
#define CDR_BURST_MIN_MS 3
#define CDR_BURST_MAX_MS 400

typedef struct {
    uint8_t band;
    bool open;
    /* Set after a burst was abandoned for running too long. A continuous
     * carrier must not be chopped into a stream of "bursts" -- it has to go
     * quiet before the detector will arm again. */
    bool suppress;
    uint32_t t_start;
    uint32_t t_last_hot; /* last sample above the exit threshold */
    int16_t peak;
    int16_t floor_at_start;
    uint8_t raw[CDR_ENV_RAW];
    uint16_t raw_len;
    uint8_t raw_step; /* samples folded into each raw slot, doubles on overflow */
    uint16_t raw_fill; /* samples accumulated into the slot being filled */
    uint16_t raw_acc;
} CdrBurstDet;

void cdr_bdet_init(CdrBurstDet* d, uint8_t band);

/** Feed one RSSI sample. Returns true and fills @p out when a burst just
 *  completed. @p enter_db lets the sensitivity setting move the trigger. */
bool cdr_bdet_push(
    CdrBurstDet* d,
    int16_t dbm,
    int16_t floor,
    uint32_t t_ms,
    uint8_t enter_db,
    CdrBurst* out);

/** Close an open burst because the radio is leaving this band. Returns true if
 *  a usable burst fell out. Without this, hopping away mid-burst silently
 *  loses it. */
bool cdr_bdet_flush(CdrBurstDet* d, uint32_t t_ms, CdrBurst* out);

/* ------------------------------------------------------------------ *
 * The learned key
 * ------------------------------------------------------------------ */

/* Learning is optional but it is what turns "some burst" into "a burst shaped
 * like the key in my pocket". Without it the engine falls back to a generic
 * keyless-entry window, which is honest but much broader, and the README says
 * so. */
#define CDR_GEN_DUR_MIN 4
#define CDR_GEN_DUR_MAX 150

/* Allowed spread around the learned length, in tenths. A key sends the same
 * frame every time; the slack is for the sampling clock, not for the key. */
#define CDR_FOB_DUR_LO 6 /* x0.6 */
#define CDR_FOB_DUR_HI 16 /* x1.6 */

/* Mean per-bin envelope distance, 0..255, below which two envelopes are "the
 * same shape". */
#define CDR_ENV_TOL 46

typedef struct {
    bool learned;
    uint8_t band;
    uint16_t dur_ms;
    int16_t peak_dbm; /* at learning distance, kept for the report only */
    uint8_t env[CDR_ENV_BINS];
} CdrFob;

/** Mean absolute per-bin difference of two envelopes, 0..255. */
uint8_t cdr_env_distance(const uint8_t* a, const uint8_t* b);

/* ------------------------------------------------------------------ *
 * Sensitivity
 * ------------------------------------------------------------------ */

typedef enum {
    CdrSensLow, /* loud, close bursts only - a busy street */
    CdrSensNormal,
    CdrSensHigh, /* a quiet driveway, catch the far ones */
    CdrSensCount,
} CdrSens;

extern const char* const cdr_sens_labels[CdrSensCount];

/** Margin over the floor a burst must reach to count as evidence. */
uint8_t cdr_sens_signal_db(uint8_t sens);
/** Margin over the floor at which a burst is opened at all. */
uint8_t cdr_sens_enter_db(uint8_t sens);

/* ------------------------------------------------------------------ *
 * Evidence
 * ------------------------------------------------------------------ */

/* Bursts kept for analysis. A relay poll train is a handful of frames a second
 * for a few seconds; sixteen covers it with room to spare, and a fixed ring
 * means the engine never allocates. */
#define CDR_EVID_RING 16

/* How far back the scorer looks. Evidence older than this has stopped being
 * about what is happening now, so it ages out and the verdict falls back on
 * its own. */
#define CDR_WINDOW_MS 20000u

typedef struct {
    CdrBurst ring[CDR_EVID_RING];
    uint8_t count; /* live entries, oldest first */
    uint32_t total_seen; /* lifetime, for the report */

    /* Beacon-guard state, maintained by cdr_evid_push rather than by the
     * scorer, because it has to outlive the 20 s evidence window: the whole
     * point is to notice that this metronome has been ticking for minutes. */
    uint32_t train_start_ms;
    uint32_t train_last_ms;
    uint16_t train_period_ms;
    bool train_valid;
} CdrEvidence;

void cdr_evid_init(CdrEvidence* e);
void cdr_evid_push(CdrEvidence* e, const CdrBurst* b);
/** Drop everything that started more than @p window_ms before @p now_ms. */
void cdr_evid_expire(CdrEvidence* e, uint32_t now_ms, uint32_t window_ms);
/** True when the periodic train currently running has been running long
 *  enough to be part of the scenery. */
bool cdr_evid_is_beacon(const CdrEvidence* e);

/* ------------------------------------------------------------------ *
 * Verdict
 * ------------------------------------------------------------------ */

typedef enum {
    CdrLevelQuiet,
    CdrLevelOdd,
    CdrLevelSuspicious,
    CdrLevelLikely,
    CdrLevelCount,
} CdrLevel;

extern const char* const cdr_level_labels[CdrLevelCount];

/* Family ceilings. They sum to 90, and the cap is 92, so a full house still
 * does not print 100 -- there is no measurement here that deserves 100. */
#define CDR_W_UNSOLICITED 40
#define CDR_W_CADENCE 30
#define CDR_W_CLONE 20
#define CDR_SCORE_CAP 92

/* Verdict thresholds. A single family, however loud, tops out at SUSPICIOUS.
 *
 * RELAY LIKELY additionally requires the CADENCE family specifically, and that
 * rule was put here by a failing test rather than by taste. Picture yourself in
 * your hallway pressing your own key six times: the bursts are unsolicited,
 * because you told the app you were away, and they are perfect clones of each
 * other, because it is one key sending one frame. Two families, sixty points,
 * and nothing whatsoever has gone wrong. The metronome is the only one of the
 * three that a human hand does not produce, so the metronome is the one the top
 * verdict is not allowed to do without. */
#define CDR_T_ODD 14
#define CDR_T_SUSPICIOUS 32
#define CDR_T_LIKELY 60
#define CDR_LIKELY_MIN_FAMILIES 2

/* Gaps outside this range are not a poll train: below it the "gap" is really
 * one frame's inter-symbol structure, above it nothing is being driven. */
#define CDR_GAP_MIN_MS 25
#define CDR_GAP_MAX_MS 3000

/* A periodic train that has been running longer than this is furniture -- a
 * weather station, a tyre sensor, a neighbour's doorbell repeater. Attacks are
 * short. Past this age CADENCE stops scoring, which is the single most
 * effective false-positive guard in the engine. */
#define CDR_BEACON_AGE_MS 60000u

#define CDR_JITTER_NA 0xFFFFu
#define CDR_PCT_NA 0xFFu

typedef struct {
    uint8_t score; /* 0..CDR_SCORE_CAP */
    uint8_t level; /* CdrLevel */

    uint8_t s_unsolicited;
    uint8_t s_cadence;
    uint8_t s_clone;
    uint8_t families; /* how many of the three scored at all */

    uint8_t matched; /* key-shaped bursts inside the window */
    uint8_t total; /* all bursts inside the window */
    uint16_t jitter_pct; /* CDR_JITTER_NA when there are too few gaps */
    uint16_t period_ms; /* mean gap, 0 when unknown */
    uint8_t clone_pct; /* CDR_PCT_NA when there are too few pairs */
    uint8_t band; /* band most of the matched bursts arrived on */
    bool beacon; /* the train looks like furniture, cadence suppressed */
} CdrVerdict;

/** Score the evidence window.
 *
 * @param armed  the operator has declared themselves away from the car. With
 *               this false the UNSOLICITED family scores nothing, because your
 *               own key explains everything, and the verdict cannot exceed
 *               SUSPICIOUS.
 * @param fob    the learned key, or NULL / .learned == false for the generic
 *               keyless-entry window.
 */
void cdr_score(
    const CdrEvidence* e,
    const CdrFob* fob,
    bool armed,
    uint8_t sens,
    uint32_t now_ms,
    CdrVerdict* out);

/** True when this burst is shaped like the key we are guarding. Exposed for
 *  the tests and for the live "shape" readout on the detail page. */
bool cdr_burst_matches(const CdrBurst* b, const CdrFob* fob, uint8_t sens);

/* ------------------------------------------------------------------ *
 * The sustained-carrier channel
 * ------------------------------------------------------------------ */

/* Reported beside the score, never folded into it. A band held open by a
 * continuous transmitter is worth knowing about -- it can be the attacker's own
 * link, and it also blinds the burst detector, which the operator deserves to
 * be told rather than left staring at a reassuringly quiet screen. Cardea says
 * so; it never reports quiet it did not earn. */
#define CDR_LINK_OCCUPANCY_PCT 60
#define CDR_LINK_MS 3000u

typedef struct {
    uint16_t acc; /* occupancy EMA, percent x100, ~256-sample window */
    uint8_t occupancy_pct;
    uint32_t sustained_ms;
    bool active;
} CdrLink;

void cdr_link_init(CdrLink* l);
/** @p hot is one observation of "this band is above the floor right now". */
void cdr_link_push(CdrLink* l, bool hot, uint32_t dt_ms);

#ifdef __cplusplus
}
#endif
