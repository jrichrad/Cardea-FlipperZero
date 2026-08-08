#include "cdr_detect.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 * Tables
 * ------------------------------------------------------------------ */

const CdrBandInfo cdr_bands[CdrBandCount] = {
    {315000000, "315.00", "315.00 MHz", "US / JP"},
    {390000000, "390.00", "390.00 MHz", "US / CA"},
    {433920000, "433.92", "433.92 MHz", "EU / IN / AU"},
    {868350000, "868.35", "868.35 MHz", "EU (newer)"},
    {915000000, "915.00", "915.00 MHz", "AU / NZ"},
};

const char* const cdr_sens_labels[CdrSensCount] = {"Low", "Normal", "High"};

const char* const cdr_level_labels[CdrLevelCount] = {
    "QUIET",
    "ODD TRAFFIC",
    "SUSPICIOUS",
    "RELAY LIKELY",
};

/* Margin over the floor a burst must clear before it is allowed to be
 * evidence. Low is for a car park with a shop radio next door; High is for a
 * driveway at 3am, where a weak reply through a house wall is exactly the
 * thing you are trying to catch. */
static const uint8_t sens_signal_db[CdrSensCount] = {16, 12, 9};
static const uint8_t sens_enter_db[CdrSensCount] = {11, CDR_BURST_ENTER_DB, 6};

uint8_t cdr_sens_signal_db(uint8_t sens) {
    return sens_signal_db[sens < CdrSensCount ? sens : CdrSensNormal];
}

uint8_t cdr_sens_enter_db(uint8_t sens) {
    return sens_enter_db[sens < CdrSensCount ? sens : CdrSensNormal];
}

/* ------------------------------------------------------------------ *
 * Noise floor
 * ------------------------------------------------------------------ */

/* Nothing on these bands has a noise floor this high. If the tracker ever
 * argues otherwise it has been fooled, and the clamp is cheaper than the
 * argument. */
#define CDR_FLOOR_MAX_DBM (-45)

void cdr_floor_init(CdrFloor* f, int16_t seed) {
    if(!f) return;
    memset(f, 0, sizeof(*f));
    if(seed != CDR_DBM_INVALID) {
        f->floor = seed;
        f->seeded = true;
    }
}

void cdr_floor_push(CdrFloor* f, int16_t dbm) {
    if(!f || dbm == CDR_DBM_INVALID) return;

    if(!f->seeded) {
        f->floor = dbm;
        f->seeded = true;
        f->leak = 0;
        f->frozen = false;
        return;
    }

    if(dbm >= (int16_t)(f->floor + CDR_FLOOR_FREEZE_DB)) {
        /* Something loud is on. Do not let it drag the floor up to meet it --
         * that is how a detector goes quiet while parked on the signal it was
         * built to find. A band that is *permanently* louder than when we
         * seeded still has to be believed eventually, so allow a relaxation,
         * but eight times slower than an ordinary one. */
        f->frozen = true;
        if(++f->leak >= (uint16_t)(CDR_FLOOR_LEAK_TICKS * 8)) {
            f->leak = 0;
            if(f->floor < CDR_FLOOR_MAX_DBM) f->floor++;
        }
        return;
    }

    f->frozen = false;

    if(dbm < f->floor) {
        f->floor = dbm; /* quiet is believed immediately */
        f->leak = 0;
        return;
    }

    if(++f->leak >= CDR_FLOOR_LEAK_TICKS) {
        f->leak = 0;
        if(f->floor < CDR_FLOOR_MAX_DBM) f->floor++;
    }
}

/* ------------------------------------------------------------------ *
 * Envelope accumulation
 * ------------------------------------------------------------------ */

/* Halve the stored resolution so a long burst keeps being described whole
 * instead of being described up to the point the buffer filled. */
static void cdr_bdet_fold(CdrBurstDet* d) {
    uint16_t n = (uint16_t)(d->raw_len / 2);
    for(uint16_t i = 0; i < n; i++) {
        d->raw[i] = (uint8_t)(((uint16_t)d->raw[2 * i] + (uint16_t)d->raw[2 * i + 1]) / 2);
    }
    d->raw_len = n;
    if(d->raw_step <= 64) d->raw_step = (uint8_t)(d->raw_step * 2);
}

static void cdr_bdet_acc(CdrBurstDet* d, int16_t margin) {
    if(margin < 0) margin = 0;
    if(margin > 255) margin = 255;

    d->raw_acc = (uint16_t)(d->raw_acc + (uint16_t)margin);
    d->raw_fill++;
    if(d->raw_fill < d->raw_step) return;

    if(d->raw_len >= CDR_ENV_RAW) cdr_bdet_fold(d);
    d->raw[d->raw_len++] = (uint8_t)(d->raw_acc / d->raw_fill);
    d->raw_acc = 0;
    d->raw_fill = 0;
}

/* Bin by slicing the stored samples eight ways. Driving the loop from the bin
 * rather than from the sample is what keeps a four-sample burst from ending up
 * with four empty bins and a nonsense shape. */
static void cdr_bdet_bin(const CdrBurstDet* d, uint8_t* env) {
    memset(env, 0, CDR_ENV_BINS);
    if(d->raw_len == 0) return;

    uint16_t v[CDR_ENV_BINS];
    uint16_t peak = 0;

    for(uint8_t b = 0; b < CDR_ENV_BINS; b++) {
        uint16_t lo = (uint16_t)(((uint32_t)b * d->raw_len) / CDR_ENV_BINS);
        uint16_t hi = (uint16_t)((((uint32_t)b + 1) * d->raw_len) / CDR_ENV_BINS);
        if(hi <= lo) hi = (uint16_t)(lo + 1);
        if(hi > d->raw_len) hi = d->raw_len;
        if(lo >= d->raw_len) lo = (uint16_t)(d->raw_len - 1);

        uint32_t sum = 0;
        uint16_t n = 0;
        for(uint16_t i = lo; i < hi; i++) {
            sum += d->raw[i];
            n++;
        }
        v[b] = n ? (uint16_t)(sum / n) : 0;
        if(v[b] > peak) peak = v[b];
    }

    /* Normalised to its own peak, so the same key at ten metres and at one
     * metre produce the same shape. Amplitude is carried separately. */
    for(uint8_t b = 0; b < CDR_ENV_BINS; b++) {
        env[b] = peak ? (uint8_t)(((uint32_t)v[b] * 255u) / peak) : 0;
    }
}

uint8_t cdr_env_distance(const uint8_t* a, const uint8_t* b) {
    if(!a || !b) return 255;
    uint32_t sum = 0;
    for(uint8_t i = 0; i < CDR_ENV_BINS; i++) {
        sum += (uint32_t)(a[i] > b[i] ? a[i] - b[i] : b[i] - a[i]);
    }
    return (uint8_t)(sum / CDR_ENV_BINS);
}

/* ------------------------------------------------------------------ *
 * Burst extraction
 * ------------------------------------------------------------------ */

void cdr_bdet_init(CdrBurstDet* d, uint8_t band) {
    if(!d) return;
    memset(d, 0, sizeof(*d));
    d->band = band;
    d->raw_step = 1;
}

static void cdr_bdet_open(CdrBurstDet* d, int16_t dbm, int16_t floor, uint32_t t_ms) {
    d->open = true;
    d->t_start = t_ms;
    d->t_last_hot = t_ms;
    d->peak = dbm;
    d->floor_at_start = floor;
    d->raw_len = 0;
    d->raw_step = 1;
    d->raw_fill = 0;
    d->raw_acc = 0;
}

/* Turn the open burst into a result. Returns false for anything too short to
 * be a frame or too long to be one. */
static bool cdr_bdet_close(CdrBurstDet* d, CdrBurst* out) {
    d->open = false;

    /* Flush whatever is still sitting in the accumulator, or a burst shorter
     * than one raw slot would arrive with no envelope at all. */
    if(d->raw_fill && d->raw_len < CDR_ENV_RAW) {
        d->raw[d->raw_len++] = (uint8_t)(d->raw_acc / d->raw_fill);
        d->raw_fill = 0;
        d->raw_acc = 0;
    }

    uint32_t dur = d->t_last_hot - d->t_start;
    if(dur < CDR_BURST_MIN_MS || dur > CDR_BURST_MAX_MS) return false;
    if(!out) return false;

    memset(out, 0, sizeof(*out));
    out->t_ms = d->t_start;
    out->dur_ms = (uint16_t)dur;
    out->peak_dbm = d->peak;
    out->floor_dbm = d->floor_at_start;
    out->band = d->band;
    cdr_bdet_bin(d, out->env);
    return true;
}

bool cdr_bdet_push(
    CdrBurstDet* d,
    int16_t dbm,
    int16_t floor,
    uint32_t t_ms,
    uint8_t enter_db,
    CdrBurst* out) {
    if(!d || dbm == CDR_DBM_INVALID || floor == CDR_DBM_INVALID) return false;

    int16_t margin = (int16_t)(dbm - floor);
    bool hot = margin >= (int16_t)enter_db;
    bool warm = margin >= CDR_BURST_EXIT_DB;

    if(!d->open) {
        if(d->suppress) {
            /* Sitting under a carrier. Wait for real silence, otherwise the
             * moment the burst was abandoned for length the next sample opens
             * an identical one and the carrier arrives as a burst train --
             * which is precisely the pattern this app calls an attack. */
            if(warm) {
                d->t_last_hot = t_ms;
            } else if((t_ms - d->t_last_hot) >= CDR_BURST_TAIL_MS) {
                d->suppress = false;
            }
            return false;
        }
        if(hot) {
            cdr_bdet_open(d, dbm, floor, t_ms);
            cdr_bdet_acc(d, margin);
        }
        return false;
    }

    if(dbm > d->peak) d->peak = dbm;
    /* Only the burst goes into the envelope. Letting the silent tail in drags
     * the last bin down and makes every burst look like it fades out. */
    if(warm) {
        d->t_last_hot = t_ms;
        cdr_bdet_acc(d, margin);
    }

    /* Held open through a fade: one deep notch inside a frame should not be
     * read as the end of one burst and the start of another. */
    if(!warm && (t_ms - d->t_last_hot) >= CDR_BURST_TAIL_MS) {
        return cdr_bdet_close(d, out);
    }

    /* Still going after this long is not a key frame. It is a carrier, and the
     * link channel is where carriers get reported. */
    if((t_ms - d->t_start) > CDR_BURST_MAX_MS) {
        d->open = false;
        d->suppress = true;
        return false;
    }

    return false;
}

bool cdr_bdet_flush(CdrBurstDet* d, uint32_t t_ms, CdrBurst* out) {
    if(!d || !d->open) return false;
    if(t_ms > d->t_last_hot) d->t_last_hot = t_ms;
    return cdr_bdet_close(d, out);
}

/* ------------------------------------------------------------------ *
 * Evidence ring
 * ------------------------------------------------------------------ */

void cdr_evid_init(CdrEvidence* e) {
    if(!e) return;
    memset(e, 0, sizeof(*e));
}

/* Is this gap the same gap the current train has been keeping? A quarter of
 * the period is generous, but the train only exists to spot machines that have
 * been running for minutes, and over minutes even a sloppy machine is obvious. */
static bool cdr_train_continues(uint16_t period, uint32_t gap) {
    if(period == 0) return false;
    uint32_t slack = period / 4;
    if(slack < 8) slack = 8;
    return gap + slack >= period && gap <= (uint32_t)period + slack;
}

void cdr_evid_push(CdrEvidence* e, const CdrBurst* b) {
    if(!e || !b) return;

    /* --- beacon-guard bookkeeping, before the ring forgets the last burst --- */
    if(e->train_valid && b->t_ms >= e->train_last_ms) {
        uint32_t gap = b->t_ms - e->train_last_ms;
        if(gap >= CDR_GAP_MIN_MS && gap <= CDR_GAP_MAX_MS) {
            if(e->train_period_ms == 0) {
                /* Second burst: this gap defines the candidate period. */
                e->train_period_ms = (uint16_t)gap;
            } else if(!cdr_train_continues(e->train_period_ms, gap)) {
                e->train_start_ms = b->t_ms;
                e->train_period_ms = 0;
            }
        } else {
            e->train_start_ms = b->t_ms;
            e->train_period_ms = 0;
        }
    } else {
        e->train_start_ms = b->t_ms;
        e->train_period_ms = 0;
        e->train_valid = true;
    }
    e->train_last_ms = b->t_ms;

    /* --- the ring itself --- */
    if(e->count == CDR_EVID_RING) {
        memmove(&e->ring[0], &e->ring[1], sizeof(CdrBurst) * (CDR_EVID_RING - 1));
        e->count--;
    }
    e->ring[e->count++] = *b;
    e->total_seen++;
}

void cdr_evid_expire(CdrEvidence* e, uint32_t now_ms, uint32_t window_ms) {
    if(!e) return;
    uint8_t keep = 0;
    for(uint8_t i = 0; i < e->count; i++) {
        if(now_ms >= e->ring[i].t_ms && (now_ms - e->ring[i].t_ms) > window_ms) continue;
        if(keep != i) e->ring[keep] = e->ring[i];
        keep++;
    }
    e->count = keep;
}

bool cdr_evid_is_beacon(const CdrEvidence* e) {
    if(!e || !e->train_valid || e->train_period_ms == 0) return false;
    return (e->train_last_ms - e->train_start_ms) > CDR_BEACON_AGE_MS;
}

/* ------------------------------------------------------------------ *
 * Matching
 * ------------------------------------------------------------------ */

bool cdr_burst_matches(const CdrBurst* b, const CdrFob* fob, uint8_t sens) {
    if(!b) return false;
    if(b->dur_ms < CDR_BURST_MIN_MS) return false;

    if(b->floor_dbm == CDR_DBM_INVALID) return false;
    if((int16_t)(b->peak_dbm - b->floor_dbm) < (int16_t)cdr_sens_signal_db(sens)) return false;

    if(fob && fob->learned) {
        if(b->band != fob->band) return false;

        uint32_t lo = ((uint32_t)fob->dur_ms * CDR_FOB_DUR_LO) / 10u;
        uint32_t hi = ((uint32_t)fob->dur_ms * CDR_FOB_DUR_HI) / 10u;
        if(lo < CDR_BURST_MIN_MS) lo = CDR_BURST_MIN_MS;
        if(hi < lo + 2) hi = lo + 2;
        if(b->dur_ms < lo || b->dur_ms > hi) return false;

        return cdr_env_distance(b->env, fob->env) <= CDR_ENV_TOL;
    }

    /* No key learned: the generic passive-entry reply window. Broad, and the
     * app says so rather than pretending otherwise. */
    return b->dur_ms >= CDR_GEN_DUR_MIN && b->dur_ms <= CDR_GEN_DUR_MAX;
}

/* ------------------------------------------------------------------ *
 * Scoring
 * ------------------------------------------------------------------ */

/* Bursts, to score. Index is the count, clamped. One is deliberately worth
 * nothing: radios are noisy, a neighbour's key exists, and a detector that
 * cries wolf at a single burst is a detector that gets switched off. */
static const uint8_t unsol_ladder[7] = {0, 0, 8, 18, 26, 33, CDR_W_UNSOLICITED};

/* The bar sits where machines are, not halfway between machines and people.
 * A relay poller is a software loop and lands under 5%; a hand on a button
 * scatters over tens of percent. Scoring the middle ground generously would
 * only buy false positives from traffic that is merely regular-ish. */
static uint8_t cdr_score_cadence(uint16_t jitter_pct) {
    if(jitter_pct <= 5) return CDR_W_CADENCE;
    if(jitter_pct <= 10) return 23;
    if(jitter_pct <= 16) return 14;
    if(jitter_pct <= 22) return 6;
    return 0;
}

static uint8_t cdr_score_clone(uint8_t clone_pct) {
    if(clone_pct >= 80) return CDR_W_CLONE;
    if(clone_pct >= 60) return 13;
    if(clone_pct >= 40) return 6;
    return 0;
}

/* Two receptions of one transmission: same length, same shape, same strength. */
static bool cdr_pair_is_clone(const CdrBurst* a, const CdrBurst* b) {
    uint16_t lo = a->dur_ms < b->dur_ms ? a->dur_ms : b->dur_ms;
    uint16_t hi = a->dur_ms < b->dur_ms ? b->dur_ms : a->dur_ms;
    if(hi == 0) return false;
    if((uint32_t)(hi - lo) * 100u > (uint32_t)hi * 15u) return false;

    int16_t da = (int16_t)(a->peak_dbm - b->peak_dbm);
    if(da < 0) da = (int16_t)-da;
    if(da > 6) return false;

    return cdr_env_distance(a->env, b->env) <= CDR_ENV_TOL;
}

void cdr_score(
    const CdrEvidence* e,
    const CdrFob* fob,
    bool armed,
    uint8_t sens,
    uint32_t now_ms,
    CdrVerdict* out) {
    if(!out) return;

    memset(out, 0, sizeof(*out));
    out->level = CdrLevelQuiet;
    out->jitter_pct = CDR_JITTER_NA;
    out->clone_pct = CDR_PCT_NA;
    out->band = CdrBand433;
    if(!e) return;

    const CdrBurst* m[CDR_EVID_RING];
    uint8_t n = 0;
    uint8_t band_hits[CdrBandCount];
    memset(band_hits, 0, sizeof(band_hits));

    for(uint8_t i = 0; i < e->count; i++) {
        const CdrBurst* b = &e->ring[i];
        if(now_ms >= b->t_ms && (now_ms - b->t_ms) > CDR_WINDOW_MS) continue;
        out->total++;
        if(!cdr_burst_matches(b, fob, sens)) continue;
        m[n++] = b;
        if(b->band < CdrBandCount) band_hits[b->band]++;
    }
    out->matched = n;

    uint8_t best = 0;
    for(uint8_t i = 1; i < CdrBandCount; i++) {
        if(band_hits[i] > band_hits[best]) best = i;
    }
    if(band_hits[best]) out->band = best;

    out->beacon = cdr_evid_is_beacon(e);

    /* --- family 1: unsolicited --- */
    if(armed) out->s_unsolicited = unsol_ladder[n > 6 ? 6 : n];

    /* Three gaps is the fewest that can distinguish a rhythm from a
     * coincidence, so four bursts is the floor for both timing families. */
    if(n >= 4) {
        uint32_t gaps[CDR_EVID_RING];
        uint8_t ng = 0;
        bool usable = true;

        for(uint8_t i = 0; i + 1 < n; i++) {
            uint32_t gap = m[i + 1]->t_ms - m[i]->t_ms;
            if(gap < CDR_GAP_MIN_MS || gap > CDR_GAP_MAX_MS) {
                usable = false;
                break;
            }
            gaps[ng++] = gap;
        }

        /* --- family 2: cadence --- */
        if(usable && ng >= 3) {
            uint32_t sum = 0;
            for(uint8_t i = 0; i < ng; i++) sum += gaps[i];
            uint32_t mean = sum / ng;

            uint32_t dev = 0;
            for(uint8_t i = 0; i < ng; i++) {
                dev += gaps[i] > mean ? gaps[i] - mean : mean - gaps[i];
            }
            dev /= ng;

            out->period_ms = (uint16_t)(mean > 0xFFFFu ? 0xFFFFu : mean);
            if(mean) {
                uint32_t j = (dev * 100u) / mean;
                out->jitter_pct = (uint16_t)(j > 999u ? 999u : j);
                out->s_cadence = cdr_score_cadence(out->jitter_pct);
            }
        }

        /* --- family 3: clone --- */
        uint8_t pairs = (uint8_t)(n - 1);
        uint8_t same = 0;
        for(uint8_t i = 0; i + 1 < n; i++) {
            if(cdr_pair_is_clone(m[i], m[i + 1])) same++;
        }
        out->clone_pct = (uint8_t)(((uint32_t)same * 100u) / pairs);
        out->s_clone = cdr_score_clone(out->clone_pct);
    }

    /* The beacon veto.
     *
     * Both timing families are statements about repetition, and a transmitter
     * that has been repeating itself, at the same interval, for over a minute
     * is the definition of benign repetition: a weather station, a tyre
     * sensor, a neighbour's doorbell repeater. So repetition stops being worth
     * anything, and the fact that it is unexplained is worth half.
     *
     * A weather station therefore settles at ODD TRAFFIC with a BEACON tag
     * instead of screaming SUSPICIOUS all night -- which matters, because a
     * detector nobody can bear to leave switched on detects nothing.
     *
     * The cost is stated plainly in the README: an attacker who kept a
     * metronome-steady poll running for more than a minute would be
     * down-weighted. No relay tool behaves that way, and the trade buys
     * immunity to every periodic sensor on the street. */
    if(out->beacon) {
        out->s_cadence = 0;
        out->s_clone = 0;
        out->s_unsolicited = (uint8_t)(out->s_unsolicited / 2);
    }

    out->families = (uint8_t)(
        (out->s_unsolicited ? 1 : 0) + (out->s_cadence ? 1 : 0) + (out->s_clone ? 1 : 0));

    uint16_t total = (uint16_t)(out->s_unsolicited + out->s_cadence + out->s_clone);
    if(total > CDR_SCORE_CAP) total = CDR_SCORE_CAP;
    out->score = (uint8_t)total;

    /* One family, however loud, is one way of being wrong. RELAY LIKELY needs
     * two independent ones to agree, and one of the two has to be the
     * metronome -- see the note by CDR_T_LIKELY for why the other two are both
     * true of an ordinary person pressing an ordinary key. Given the weights,
     * this also means the top verdict can only ever be reached while armed. */
    if(out->score >= CDR_T_LIKELY && out->families >= CDR_LIKELY_MIN_FAMILIES &&
       out->s_cadence > 0) {
        out->level = CdrLevelLikely;
    } else if(out->score >= CDR_T_SUSPICIOUS) {
        out->level = CdrLevelSuspicious;
    } else if(out->score >= CDR_T_ODD) {
        out->level = CdrLevelOdd;
    } else {
        out->level = CdrLevelQuiet;
    }
}

/* ------------------------------------------------------------------ *
 * Sustained carrier
 * ------------------------------------------------------------------ */

void cdr_link_init(CdrLink* l) {
    if(!l) return;
    memset(l, 0, sizeof(*l));
}

void cdr_link_push(CdrLink* l, bool hot, uint32_t dt_ms) {
    if(!l) return;

    /* ~256-sample window. Short enough to notice a link coming up inside a few
     * hundred milliseconds, long enough that one key frame does not read as a
     * band held open. */
    uint32_t acc = ((uint32_t)l->acc * 255u + (hot ? 10000u : 0u)) / 256u;
    l->acc = (uint16_t)(acc > 10000u ? 10000u : acc);
    l->occupancy_pct = (uint8_t)(l->acc / 100u);

    if(l->occupancy_pct >= CDR_LINK_OCCUPANCY_PCT) {
        l->sustained_ms += dt_ms;
    } else {
        l->sustained_ms = 0;
    }
    l->active = l->sustained_ms >= CDR_LINK_MS;
}
