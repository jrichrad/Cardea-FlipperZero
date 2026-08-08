/* Host tests for the Cardea evidence engine.
 *
 * The engine is the product. Everything else -- the radio worker, the views,
 * the icons -- can be checked by looking at it. Whether a stream of RSSI
 * samples is read correctly cannot, so it is checked here, on a desktop, with
 * signals whose answers are known in advance.
 *
 *     make -C test
 */
#include "../helpers/cdr_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned checks = 0;
static unsigned failures = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        checks++;                                           \
        if(!(cond)) {                                       \
            failures++;                                     \
            printf("  FAIL line %-4d  ", __LINE__);         \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while(0)

#define CHECK_EQ(got, want, label)                                                       \
    do {                                                                                 \
        long g_ = (long)(got), w_ = (long)(want);                                        \
        checks++;                                                                        \
        if(g_ != w_) {                                                                   \
            failures++;                                                                  \
            printf("  FAIL line %-4d  %s: got %ld, want %ld\n", __LINE__, label, g_, w_); \
        }                                                                                \
    } while(0)

static void section(const char* name) {
    printf("\n%s\n", name);
}

/* ------------------------------------------------------------------ *
 * Signal generators
 * ------------------------------------------------------------------ */

typedef uint8_t (*ShapeFn)(uint16_t i, uint16_t len);

static uint8_t shape_flat(uint16_t i, uint16_t len) {
    (void)i;
    (void)len;
    return 100;
}

/* Quiet at the edges, loud in the middle - a keyed frame seen through AGC. */
static uint8_t shape_hump(uint16_t i, uint16_t len) {
    if(len == 0) return 100;
    uint16_t half = (uint16_t)(len / 2);
    uint16_t d = i > half ? (uint16_t)(i - half) : (uint16_t)(half - i);
    uint32_t v = half ? (100u - (60u * d) / half) : 100u;
    return (uint8_t)v;
}

/* Loud then trailing off. Deliberately a different shape from the hump so the
 * envelope comparison has something to disagree about. */
static uint8_t shape_decay(uint16_t i, uint16_t len) {
    if(len == 0) return 100;
    uint32_t v = 100u - (70u * i) / len;
    return (uint8_t)v;
}

/* Push one burst plus a silent tail, at 1 kHz. Returns the extracted burst. */
static bool feed_burst(
    CdrBurstDet* d,
    int16_t floor,
    uint32_t* t,
    uint16_t dur,
    uint8_t peak_db,
    ShapeFn shape,
    CdrBurst* out) {
    bool got = false;
    CdrBurst scratch;
    uint8_t enter = cdr_sens_enter_db(CdrSensNormal);

    for(uint16_t i = 0; i <= dur; i++) {
        int16_t dbm = (int16_t)(floor + (int16_t)(((uint32_t)peak_db * shape(i, dur)) / 100u));
        if(cdr_bdet_push(d, dbm, floor, *t, enter, &scratch)) {
            if(out) *out = scratch;
            got = true;
        }
        (*t)++;
    }
    for(uint16_t i = 0; i < 20; i++) {
        if(cdr_bdet_push(d, floor, floor, *t, enter, &scratch)) {
            if(out) *out = scratch;
            got = true;
        }
        (*t)++;
    }
    return got;
}

/* A burst built by hand, for the scoring tests, where the extraction path is
 * not what is under test. */
static CdrBurst mk(uint32_t t_ms, uint16_t dur, int16_t peak, uint8_t band, ShapeFn shape) {
    CdrBurst b;
    memset(&b, 0, sizeof(b));
    b.t_ms = t_ms;
    b.dur_ms = dur;
    b.peak_dbm = peak;
    b.floor_dbm = -100;
    b.band = band;

    uint16_t peak_bin = 0;
    uint16_t v[CDR_ENV_BINS];
    for(uint8_t i = 0; i < CDR_ENV_BINS; i++) {
        v[i] = shape(i, CDR_ENV_BINS - 1);
        if(v[i] > peak_bin) peak_bin = v[i];
    }
    for(uint8_t i = 0; i < CDR_ENV_BINS; i++) {
        b.env[i] = peak_bin ? (uint8_t)(((uint32_t)v[i] * 255u) / peak_bin) : 0;
    }
    return b;
}

/* ------------------------------------------------------------------ *
 * Noise floor
 * ------------------------------------------------------------------ */

static void t_floor(void) {
    section("noise floor");

    CdrFloor f;
    cdr_floor_init(&f, CDR_DBM_INVALID);
    CHECK(!f.seeded, "an uninitialised floor must not claim a reading");

    cdr_floor_push(&f, -96);
    CHECK(f.seeded, "first sample seeds the floor");
    CHECK_EQ(f.floor, -96, "seeded floor");

    /* Quiet is believed at once. */
    cdr_floor_push(&f, -103);
    CHECK_EQ(f.floor, -103, "floor follows a quieter sample immediately");

    /* Noise is believed slowly. */
    for(int i = 0; i < CDR_FLOOR_LEAK_TICKS - 1; i++) cdr_floor_push(&f, -99);
    CHECK_EQ(f.floor, -103, "floor has not relaxed yet");
    cdr_floor_push(&f, -99);
    CHECK_EQ(f.floor, -102, "floor relaxes one dB per leak interval");

    /* THE regression. A carrier well above the floor must not drag it up:
     * if it does, the margin collapses and the detector goes silent while
     * sitting on the very transmission it exists to catch. */
    cdr_floor_init(&f, -100);
    for(int i = 0; i < 400; i++) cdr_floor_push(&f, -70);
    CHECK(f.frozen, "a strong carrier freezes the floor");
    CHECK(f.floor <= -99, "carrier must not walk the floor up (floor=%d)", f.floor);
    CHECK(
        (-70 - f.floor) >= CDR_BURST_ENTER_DB,
        "the carrier is still above the burst trigger (margin=%d)",
        -70 - f.floor);

    /* But a band that is permanently louder has to be believed eventually,
     * or the app never adapts to a new parking spot. */
    cdr_floor_init(&f, -110);
    for(int i = 0; i < CDR_FLOOR_LEAK_TICKS * 8 * 4; i++) cdr_floor_push(&f, -80);
    CHECK(f.floor > -110, "a permanent ambient rise eventually moves the floor");
    CHECK(f.floor < -95, "...but slowly (floor=%d)", f.floor);

    /* The clamp. */
    cdr_floor_init(&f, -50);
    for(int i = 0; i < 100000; i++) cdr_floor_push(&f, -46);
    CHECK(f.floor <= -45, "floor is clamped to something physically sane");
}

/* ------------------------------------------------------------------ *
 * Envelopes
 * ------------------------------------------------------------------ */

static void t_env(void) {
    section("envelopes");

    uint8_t a[CDR_ENV_BINS] = {0, 36, 72, 109, 145, 182, 218, 255};
    uint8_t b[CDR_ENV_BINS] = {0, 36, 72, 109, 145, 182, 218, 255};
    uint8_t c[CDR_ENV_BINS] = {255, 218, 182, 145, 109, 72, 36, 0};

    CHECK_EQ(cdr_env_distance(a, b), 0, "identical envelopes");
    CHECK(cdr_env_distance(a, c) > CDR_ENV_TOL, "a ramp and its mirror are different shapes");
    CHECK(cdr_env_distance(a, c) >= 100, "...and obviously so");

    /* Small fading between two receptions of the same frame must stay inside
     * tolerance, or CLONE never fires on real air. */
    uint8_t d[CDR_ENV_BINS];
    for(uint8_t i = 0; i < CDR_ENV_BINS; i++) {
        int v = a[i] + ((i % 2) ? 18 : -14);
        d[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    CHECK(cdr_env_distance(a, d) <= CDR_ENV_TOL, "ordinary fading stays a match");
}

/* ------------------------------------------------------------------ *
 * Burst extraction
 * ------------------------------------------------------------------ */

static void t_burst(void) {
    section("burst extraction");

    CdrBurstDet d;
    CdrBurst out;
    uint32_t t = 1000;
    const int16_t floor = -100;

    cdr_bdet_init(&d, CdrBand433);
    CHECK(feed_burst(&d, floor, &t, 40, 25, shape_flat, &out), "a 40 ms burst is detected");
    CHECK_EQ(out.dur_ms, 40, "reported duration");
    CHECK_EQ(out.peak_dbm, -75, "reported peak");
    CHECK_EQ(out.floor_dbm, floor, "the floor it stood above");
    CHECK_EQ(out.band, CdrBand433, "band tag");
    CHECK_EQ(out.t_ms, 1000, "start timestamp");

    /* A flat burst normalises to a flat envelope. */
    cdr_bdet_init(&d, CdrBand433);
    t = 0;
    feed_burst(&d, floor, &t, 60, 25, shape_flat, &out);
    for(uint8_t i = 0; i < CDR_ENV_BINS; i++) {
        CHECK(out.env[i] >= 250, "flat burst bin %u is at full scale (%u)", i, out.env[i]);
    }

    /* A humped burst does not. */
    cdr_bdet_init(&d, CdrBand433);
    t = 0;
    feed_burst(&d, floor, &t, 60, 25, shape_hump, &out);
    CHECK(out.env[0] < 200, "humped burst starts low (%u)", out.env[0]);
    CHECK(out.env[CDR_ENV_BINS / 2] >= 240, "...peaks in the middle");
    CHECK(out.env[CDR_ENV_BINS - 1] < 200, "...and ends low");

    /* Too short to be a frame. */
    cdr_bdet_init(&d, CdrBand433);
    t = 0;
    CHECK(!feed_burst(&d, floor, &t, 1, 25, shape_flat, &out), "a 1 ms blip is not a burst");

    /* Too long to be a frame - that is a carrier, and the link channel owns
     * carriers. */
    cdr_bdet_init(&d, CdrBand433);
    t = 0;
    CHECK(
        !feed_burst(&d, floor, &t, CDR_BURST_MAX_MS + 100, 25, shape_flat, &out),
        "a held carrier is not reported as a burst");

    /* A notch inside a frame must not split it in two. */
    cdr_bdet_init(&d, CdrBand433);
    t = 0;
    uint8_t enter = cdr_sens_enter_db(CdrSensNormal);
    int splits = 0;
    for(uint16_t i = 0; i < 60; i++) {
        /* 3 ms dip to just above the exit threshold, in the middle */
        int16_t dbm = (int16_t)(floor + ((i >= 28 && i < 31) ? 6 : 25));
        if(cdr_bdet_push(&d, dbm, floor, t++, enter, &out)) splits++;
    }
    for(uint16_t i = 0; i < 20; i++) {
        if(cdr_bdet_push(&d, floor, floor, t++, enter, &out)) splits++;
    }
    CHECK_EQ(splits, 1, "a fade inside a frame yields one burst, not two");
    CHECK(out.dur_ms >= 58, "...spanning the whole frame (%u ms)", out.dur_ms);

    /* Hopping away mid-burst must not lose it silently. */
    cdr_bdet_init(&d, CdrBand868);
    t = 0;
    for(uint16_t i = 0; i < 30; i++) cdr_bdet_push(&d, (int16_t)(floor + 25), floor, t++, enter, &out);
    CHECK(cdr_bdet_flush(&d, t, &out), "flush closes an open burst when the radio leaves");
    CHECK_EQ(out.band, CdrBand868, "flushed burst keeps its band");
    CHECK(!cdr_bdet_flush(&d, t, &out), "flushing twice yields nothing");

    /* A burst far longer than the raw buffer still describes its whole shape
     * rather than only its first 128 ms. */
    cdr_bdet_init(&d, CdrBand433);
    t = 0;
    feed_burst(&d, floor, &t, 380, 30, shape_decay, &out);
    CHECK_EQ(out.dur_ms, 380, "long burst duration survives decimation");
    CHECK(out.env[0] >= 240, "long burst still starts loud (%u)", out.env[0]);
    CHECK(
        out.env[CDR_ENV_BINS - 1] < 130,
        "...and the decay at the far end is still visible (%u)",
        out.env[CDR_ENV_BINS - 1]);
}

/* ------------------------------------------------------------------ *
 * Shape matching
 * ------------------------------------------------------------------ */

static void t_match(void) {
    section("key matching");

    CdrBurst b = mk(0, 40, -70, CdrBand433, shape_hump);

    /* No key learned: the generic window. */
    CHECK(cdr_burst_matches(&b, NULL, CdrSensNormal), "generic window accepts a 40 ms burst");

    CdrBurst tiny = mk(0, 2, -70, CdrBand433, shape_hump);
    CHECK(!cdr_burst_matches(&tiny, NULL, CdrSensNormal), "generic window rejects a 2 ms blip");

    CdrBurst slow = mk(0, 300, -70, CdrBand433, shape_hump);
    CHECK(!cdr_burst_matches(&slow, NULL, CdrSensNormal), "generic window rejects a 300 ms burst");

    /* Weak bursts are rejected, and how weak depends on sensitivity. */
    CdrBurst weak = mk(0, 40, -89, CdrBand433, shape_hump); /* 11 dB over floor */
    CHECK(!cdr_burst_matches(&weak, NULL, CdrSensNormal), "11 dB is below the Normal gate");
    CHECK(cdr_burst_matches(&weak, NULL, CdrSensHigh), "...but above the High gate");
    CHECK(!cdr_burst_matches(&b, NULL, CdrSensLow) == false, "30 dB clears every gate");

    /* A learned key narrows all three axes. */
    CdrFob fob;
    memset(&fob, 0, sizeof(fob));
    fob.learned = true;
    fob.band = CdrBand433;
    fob.dur_ms = 40;
    fob.peak_dbm = -50;
    memcpy(fob.env, b.env, CDR_ENV_BINS);

    CHECK(cdr_burst_matches(&b, &fob, CdrSensNormal), "the learned key matches itself");

    CdrBurst wrong_band = mk(0, 40, -70, CdrBand868, shape_hump);
    CHECK(!cdr_burst_matches(&wrong_band, &fob, CdrSensNormal), "wrong band is rejected");

    CdrBurst wrong_len = mk(0, 90, -70, CdrBand433, shape_hump);
    CHECK(!cdr_burst_matches(&wrong_len, &fob, CdrSensNormal), "wrong length is rejected");

    CdrBurst wrong_shape = mk(0, 40, -70, CdrBand433, shape_decay);
    CHECK(!cdr_burst_matches(&wrong_shape, &fob, CdrSensNormal), "wrong envelope is rejected");

    /* ...but tolerates the slop a real reception has. */
    CdrBurst close_enough = mk(0, 44, -78, CdrBand433, shape_hump);
    CHECK(
        cdr_burst_matches(&close_enough, &fob, CdrSensNormal),
        "the same key, weaker and 4 ms longer, still matches");
}

/* ------------------------------------------------------------------ *
 * Evidence ring
 * ------------------------------------------------------------------ */

static void t_evidence(void) {
    section("evidence ring");

    CdrEvidence e;
    cdr_evid_init(&e);
    CHECK_EQ(e.count, 0, "starts empty");

    for(int i = 0; i < CDR_EVID_RING + 5; i++) {
        CdrBurst b = mk((uint32_t)(i * 100), 40, -70, CdrBand433, shape_hump);
        cdr_evid_push(&e, &b);
    }
    CHECK_EQ(e.count, CDR_EVID_RING, "the ring is bounded");
    CHECK_EQ(e.total_seen, CDR_EVID_RING + 5, "the lifetime counter is not");
    CHECK_EQ(e.ring[0].t_ms, 500, "the oldest entries were dropped, not the newest");

    /* Ageing out. */
    cdr_evid_init(&e);
    for(int i = 0; i < 8; i++) {
        CdrBurst b = mk((uint32_t)(i * 5000), 40, -70, CdrBand433, shape_hump);
        cdr_evid_push(&e, &b);
    }
    cdr_evid_expire(&e, 35000, CDR_WINDOW_MS);
    CHECK_EQ(e.count, 5, "everything older than the window is gone");
    CHECK_EQ(e.ring[0].t_ms, 15000, "and the survivors are the recent ones");
}

/* ------------------------------------------------------------------ *
 * Beacon guard
 * ------------------------------------------------------------------ */

static void t_beacon(void) {
    section("beacon guard");

    CdrEvidence e;
    cdr_evid_init(&e);
    CHECK(!cdr_evid_is_beacon(&e), "nothing seen yet is not a beacon");

    /* Half a minute of metronome is not yet furniture. */
    for(uint32_t t = 0; t <= 30000; t += 500) {
        CdrBurst b = mk(t, 40, -70, CdrBand433, shape_hump);
        cdr_evid_push(&e, &b);
    }
    CHECK(!cdr_evid_is_beacon(&e), "30 s of metronome is not yet furniture");

    /* A minute and a half is. */
    for(uint32_t t = 30500; t <= 90000; t += 500) {
        CdrBurst b = mk(t, 40, -70, CdrBand433, shape_hump);
        cdr_evid_push(&e, &b);
    }
    CHECK(cdr_evid_is_beacon(&e), "90 s of metronome is scenery");

    /* Breaking the rhythm restarts the clock - a relay burst arriving in a
     * street that also has a weather station must not inherit its age. */
    CdrBurst odd = mk(92500, 40, -70, CdrBand433, shape_hump);
    cdr_evid_push(&e, &odd);
    CHECK(!cdr_evid_is_beacon(&e), "a broken rhythm is a new train");

    /* A burst train with no rhythm at all never becomes a beacon. */
    cdr_evid_init(&e);
    uint32_t t = 0;
    unsigned seed = 7;
    for(int i = 0; i < 200; i++) {
        seed = seed * 1103515245u + 12345u;
        t += 200 + (seed >> 24) % 900;
        CdrBurst b = mk(t, 40, -70, CdrBand433, shape_hump);
        cdr_evid_push(&e, &b);
    }
    CHECK(!cdr_evid_is_beacon(&e), "ragged traffic never becomes a beacon");
}

/* ------------------------------------------------------------------ *
 * Scoring
 * ------------------------------------------------------------------ */

/* Fill a ring with n bursts at a fixed period, optionally with jitter, all the
 * same shape (so they read as clones) unless varied is set. */
static void
    train(CdrEvidence* e, int n, uint32_t period, uint32_t jitter, bool varied, uint32_t start) {
    unsigned seed = 12345;
    uint32_t t = start;
    for(int i = 0; i < n; i++) {
        ShapeFn s = shape_hump;
        uint16_t dur = 40;
        int16_t peak = -70;
        if(varied) {
            seed = seed * 1103515245u + 12345u;
            s = (seed >> 20) & 1 ? shape_hump : shape_decay;
            dur = (uint16_t)(20 + ((seed >> 16) % 60));
            peak = (int16_t)(-80 + (int16_t)((seed >> 12) % 20));
        }
        CdrBurst b = mk(t, dur, peak, CdrBand433, s);
        cdr_evid_push(e, &b);
        uint32_t g = period;
        if(jitter) {
            seed = seed * 1103515245u + 12345u;
            g = period - jitter + (seed >> 22) % (2 * jitter + 1);
        }
        t += g;
    }
}

/* A train with hand-written gaps, so "this is what a person looks like" is
 * stated by the test rather than left to a random number generator. */
static void train_gaps(CdrEvidence* e, const uint32_t* gaps, int ngaps, uint32_t start) {
    uint32_t t = start;
    for(int i = 0; i <= ngaps; i++) {
        CdrBurst b = mk(t, 40, -70, CdrBand433, shape_hump);
        cdr_evid_push(e, &b);
        if(i < ngaps) t += gaps[i];
    }
}

static void t_score(void) {
    section("scoring");

    CdrEvidence e;
    CdrVerdict v;

    /* Silence. */
    cdr_evid_init(&e);
    cdr_score(&e, NULL, true, CdrSensNormal, 10000, &v);
    CHECK_EQ(v.level, CdrLevelQuiet, "an empty window is quiet");
    CHECK_EQ(v.score, 0, "an empty window scores nothing");
    CHECK_EQ(v.jitter_pct, CDR_JITTER_NA, "no jitter figure without gaps");

    /* One burst. Somebody, somewhere, pressed a key. */
    cdr_evid_init(&e);
    train(&e, 1, 0, 0, false, 1000);
    cdr_score(&e, NULL, true, CdrSensNormal, 2000, &v);
    CHECK_EQ(v.matched, 1, "the burst was matched");
    CHECK_EQ(v.score, 0, "one burst is never evidence");
    CHECK_EQ(v.level, CdrLevelQuiet, "one burst is never an alert");

    /* Two. Interesting, not alarming. */
    cdr_evid_init(&e);
    train(&e, 2, 600, 0, false, 1000);
    cdr_score(&e, NULL, true, CdrSensNormal, 3000, &v);
    CHECK_EQ(v.score, 8, "two bursts is a nudge");
    CHECK_EQ(v.level, CdrLevelQuiet, "...still below ODD");

    /* The signature: six identical frames on a 220 ms metronome, while the
     * operator has declared themselves away from the car. */
    cdr_evid_init(&e);
    train(&e, 6, 220, 0, false, 1000);
    cdr_score(&e, NULL, true, CdrSensNormal, 3000, &v);
    CHECK_EQ(v.matched, 6, "all six matched");
    CHECK_EQ(v.s_unsolicited, CDR_W_UNSOLICITED, "unsolicited maxed");
    CHECK_EQ(v.s_cadence, CDR_W_CADENCE, "a perfect metronome maxes cadence");
    CHECK_EQ(v.s_clone, CDR_W_CLONE, "identical frames max clone");
    CHECK_EQ(v.families, 3, "all three families agree");
    CHECK_EQ(v.level, CdrLevelLikely, "that is a relay");
    CHECK_EQ(v.period_ms, 220, "the poll period is reported");
    CHECK_EQ(v.jitter_pct, 0, "jitter is reported");
    CHECK(v.score <= CDR_SCORE_CAP, "score respects the cap");
    CHECK(v.score < 100, "the engine never claims certainty (%u)", v.score);

    /* Exactly the same air, but the operator is standing at the car. Their own
     * key explains all of it. */
    cdr_score(&e, NULL, false, CdrSensNormal, 3000, &v);
    CHECK_EQ(v.s_unsolicited, 0, "disarmed, unexplained traffic is explained");
    CHECK(v.level <= CdrLevelSuspicious, "disarmed can never reach RELAY LIKELY");

    /* Somebody thumbing a key by hand. Same six frames, same key, same band --
     * only the rhythm differs, and the rhythm is the whole point. */
    cdr_evid_init(&e);
    const uint32_t human[5] = {380, 1450, 620, 2100, 900};
    train_gaps(&e, human, 5, 1000);
    cdr_score(&e, NULL, true, CdrSensHigh, 7000, &v);
    CHECK(v.matched == 6, "all six were still seen and matched");
    CHECK(v.s_cadence == 0, "human timing is not a metronome (jitter=%u%%)", v.jitter_pct);
    CHECK(v.s_clone == CDR_W_CLONE, "one key does send identical frames");
    CHECK(v.s_unsolicited == CDR_W_UNSOLICITED, "and you did say you were away");
    /* Two families and sixty points, from somebody standing in their own
     * hallway. This is why the metronome is required. */
    CHECK(v.level == CdrLevelSuspicious, "a person with a key is suspicious, not a relay");

    /* And a machine is, on the very same frames. This pair is the experiment
     * the whole CADENCE family rests on. */
    cdr_evid_init(&e);
    const uint32_t machine[5] = {200, 205, 198, 203, 199};
    train_gaps(&e, machine, 5, 1000);
    cdr_score(&e, NULL, true, CdrSensHigh, 7000, &v);
    CHECK(v.s_cadence == CDR_W_CADENCE, "a software poll loop is (jitter=%u%%)", v.jitter_pct);
    CHECK(v.level == CdrLevelLikely, "identical frames on a metronome, while away: relay");

    /* A weather station: metronome, identical frames, but running all
     * evening. */
    cdr_evid_init(&e);
    train(&e, 200, 500, 0, false, 0);
    cdr_score(&e, NULL, true, CdrSensNormal, 100000, &v);
    CHECK(v.beacon, "the long-running metronome is flagged as a beacon");
    CHECK_EQ(v.s_cadence, 0, "beacon vetoes cadence");
    CHECK_EQ(v.s_clone, 0, "beacon vetoes clone");
    CHECK_EQ(v.level, CdrLevelOdd, "a weather station settles at ODD TRAFFIC");
    CHECK(v.level < CdrLevelSuspicious, "...and never nags");

    /* A relay burst is short, so it never inherits the beacon flag. */
    cdr_evid_init(&e);
    train(&e, 8, 200, 0, false, 0);
    cdr_score(&e, NULL, true, CdrSensNormal, 2000, &v);
    CHECK(!v.beacon, "a two-second train is not a beacon");
    CHECK_EQ(v.level, CdrLevelLikely, "and it is still called");

    /* Cadence and clone alone - a metronome of identical frames that the
     * operator has NOT declared themselves away from - must not reach LIKELY,
     * because the top verdict is reserved for traffic nobody can explain. */
    cdr_evid_init(&e);
    train(&e, 6, 220, 0, false, 1000);
    cdr_score(&e, NULL, false, CdrSensNormal, 3000, &v);
    CHECK_EQ(v.s_cadence + v.s_clone, CDR_W_CADENCE + CDR_W_CLONE, "both timing families fire");
    CHECK(v.score < CDR_T_LIKELY, "...and together they still fall short of LIKELY");

    /* A learned key filters out the neighbourhood. Same relay-shaped train,
     * but on the wrong band for the key we are guarding. */
    CdrFob fob;
    memset(&fob, 0, sizeof(fob));
    fob.learned = true;
    fob.band = CdrBand868;
    fob.dur_ms = 40;
    CdrBurst tmpl = mk(0, 40, -70, CdrBand868, shape_hump);
    memcpy(fob.env, tmpl.env, CDR_ENV_BINS);

    cdr_evid_init(&e);
    train(&e, 6, 220, 0, false, 1000);
    cdr_score(&e, &fob, true, CdrSensNormal, 3000, &v);
    CHECK_EQ(v.total, 6, "the bursts were seen");
    CHECK_EQ(v.matched, 0, "...but none of them was our key");
    CHECK_EQ(v.level, CdrLevelQuiet, "a learned key silences the neighbours");

    /* Evidence ages out on its own, so a finished event stops alerting. */
    cdr_evid_init(&e);
    train(&e, 6, 220, 0, false, 1000);
    cdr_evid_expire(&e, 60000, CDR_WINDOW_MS);
    cdr_score(&e, NULL, true, CdrSensNormal, 60000, &v);
    CHECK_EQ(v.total, 0, "the window emptied");
    CHECK_EQ(v.level, CdrLevelQuiet, "and the alarm stood down by itself");
}

/* ------------------------------------------------------------------ *
 * Sustained carrier
 * ------------------------------------------------------------------ */

static void t_link(void) {
    section("sustained carrier");

    CdrLink l;
    cdr_link_init(&l);
    CHECK(!l.active, "starts inactive");

    /* One key frame is not a link. */
    for(int i = 0; i < 60; i++) cdr_link_push(&l, true, 1);
    CHECK(!l.active, "a 60 ms frame does not count as a held band");

    /* A band actually held open does. */
    cdr_link_init(&l);
    for(int i = 0; i < 5000; i++) cdr_link_push(&l, true, 1);
    CHECK(l.active, "five seconds of carrier is a held band");
    CHECK(l.occupancy_pct >= CDR_LINK_OCCUPANCY_PCT, "occupancy is reported (%u%%)", l.occupancy_pct);

    /* And it stands down. */
    for(int i = 0; i < 5000; i++) cdr_link_push(&l, false, 1);
    CHECK(!l.active, "the link clears when the carrier stops");
    CHECK_EQ(l.sustained_ms, 0, "the sustained timer resets");

    /* Bursty traffic at 30% duty is not a link. */
    cdr_link_init(&l);
    for(int i = 0; i < 10000; i++) cdr_link_push(&l, (i % 10) < 3, 1);
    CHECK(!l.active, "30%% duty is traffic, not a held band (occ=%u%%)", l.occupancy_pct);
}

/* ------------------------------------------------------------------ *
 * Invariants
 * ------------------------------------------------------------------ */

/* Properties that must hold for every input the engine can be handed. These
 * are the promises the README makes, so they are checked against several
 * thousand randomly generated situations rather than against the handful of
 * cases somebody thought of. */
static void t_invariants(void) {
    section("invariants (randomised)");

    unsigned seed = 20260807u;
    unsigned likely_seen = 0;

    for(int iter = 0; iter < 3000; iter++) {
        seed = seed * 1103515245u + 12345u;
        int n = (int)((seed >> 16) % 12);
        seed = seed * 1103515245u + 12345u;
        uint32_t period = 30 + (seed >> 14) % 3200;
        seed = seed * 1103515245u + 12345u;
        uint32_t jitter = (seed >> 18) % 400;
        seed = seed * 1103515245u + 12345u;
        bool varied = ((seed >> 19) & 1) != 0;
        seed = seed * 1103515245u + 12345u;
        bool armed = ((seed >> 21) & 1) != 0;
        seed = seed * 1103515245u + 12345u;
        uint8_t sens = (uint8_t)((seed >> 23) % CdrSensCount);

        CdrEvidence e;
        cdr_evid_init(&e);
        train(&e, n, period, jitter > period / 2 ? period / 2 : jitter, varied, 1000);

        CdrVerdict v;
        cdr_score(&e, NULL, armed, sens, 1000 + (uint32_t)n * period + 10, &v);

        CHECK(v.score <= CDR_SCORE_CAP, "score never exceeds the cap (%u)", v.score);
        CHECK(v.level < CdrLevelCount, "level is a valid enum");
        CHECK(v.matched <= v.total, "matched is a subset of total");
        CHECK(v.total <= CDR_EVID_RING, "total cannot exceed the ring");
        CHECK(
            v.s_unsolicited <= CDR_W_UNSOLICITED && v.s_cadence <= CDR_W_CADENCE &&
                v.s_clone <= CDR_W_CLONE,
            "no family exceeds its ceiling");

        if(v.level == CdrLevelLikely) {
            likely_seen++;
            CHECK(armed, "RELAY LIKELY is only reachable while armed");
            CHECK(v.families >= CDR_LIKELY_MIN_FAMILIES, "RELAY LIKELY needs two families");
            CHECK(v.s_cadence > 0, "RELAY LIKELY always includes the metronome");
            CHECK(v.matched >= 3, "RELAY LIKELY needs a train, not a burst");
        }
        if(v.matched <= 1) {
            CHECK_EQ(v.score, 0, "one burst or none can never score");
        }
        if(v.beacon) {
            CHECK_EQ(v.s_cadence, 0, "a beacon never scores cadence");
            CHECK_EQ(v.s_clone, 0, "a beacon never scores clone");
        }
    }

    /* If the randomiser never produced an alert, the invariants above proved
     * nothing about alerts. */
    CHECK(likely_seen > 0, "the random sweep did reach RELAY LIKELY at least once");
    printf("  (RELAY LIKELY reached in %u of 3000 random situations)\n", likely_seen);
}

/* ------------------------------------------------------------------ */

int main(void) {
    printf("Cardea - evidence engine\n");
    printf("========================\n");

    t_floor();
    t_env();
    t_burst();
    t_match();
    t_evidence();
    t_beacon();
    t_score();
    t_link();
    t_invariants();

    printf("\n------------------------\n");
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
