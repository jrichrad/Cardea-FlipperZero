#include "cdr_radio.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_subghz.h> /* FuriHalSubGhzPreset */
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#include <string.h>

#define TAG "Cardea"

/* CdrWork is a big local -- the evidence ring alone is a few hundred bytes,
 * and the burst detector carries a 128-sample buffer -- and cdr_score adds two
 * sixteen-entry arrays on top of it. Two kilobytes would probably do; three
 * costs nothing and removes the question. */
#define CDR_WORKER_STACK (3 * 1024)

/* The narrowest stock RX filter, 270 kHz. A key fob frame is a few tens of
 * kHz wide, so the narrow filter both hears it better and keeps the band next
 * door out of the measurement. */
#define CDR_PRESET FuriHalSubGhzPresetOok270Async

/* Settling time after a retune, before the first believable RSSI reading. */
#define CDR_SETTLE_US 500

/* Sampling period while camped. Deliberately just under a millisecond: burst
 * lengths are the second axis of the key template, so they are worth
 * measuring at a resolution finer than the thing being measured. */
#define CDR_SAMPLE_US 800

/* How long the worker stays inside the camped sampling loop before coming back
 * up for requests, scoring and publication. */
#define CDR_CAMP_SLICE_MS 20

struct CdrRadio {
    FuriThread* thread;
    FuriMutex* mutex;
    volatile bool running;

    /* requests, consumed by the worker */
    CdrRadioMode mode;
    uint8_t req_band_mask;
    uint8_t req_sens;
    uint8_t req_pin; /* CdrBandCount == hop */
    bool req_armed;
    bool req_reset;
    CdrFob req_fob;

    CdrSnapshot snap;
};

/* ------------------------------------------------------------------ *
 * Small helpers
 * ------------------------------------------------------------------ */

static uint32_t cdr_ms_since(uint32_t start_tick, uint32_t hz) {
    uint32_t now = furi_get_tick();
    return (uint32_t)(((uint64_t)(now - start_tick) * 1000u) / hz);
}

static uint8_t cdr_first_band(uint8_t mask) {
    for(uint8_t i = 0; i < CdrBandCount; i++) {
        if(mask & (uint8_t)(1u << i)) return i;
    }
    return CdrBand433;
}

static bool cdr_tune(const SubGhzDevice* device, uint8_t band) {
    if(band >= CdrBandCount) return false;
    uint32_t freq = cdr_bands[band].freq;
    if(!subghz_devices_is_frequency_valid(device, freq)) return false;

    subghz_devices_idle(device);
    subghz_devices_set_frequency(device, freq);
    subghz_devices_flush_rx(device);
    subghz_devices_set_rx(device);
    furi_delay_us(CDR_SETTLE_US);
    return true;
}

/* Scale a burst's margin over the floor into a raster column. */
static uint8_t cdr_raster_level(const CdrBurst* b) {
    int16_t margin = (int16_t)(b->peak_dbm - b->floor_dbm);
    if(margin < 1) margin = 1;
    if(margin > CDR_RASTER_MAX_DB) margin = CDR_RASTER_MAX_DB;
    return (uint8_t)margin;
}

/* ------------------------------------------------------------------ *
 * Worker state
 * ------------------------------------------------------------------ */

typedef struct {
    CdrFloor floors[CdrBandCount];
    CdrBurstDet bdet;
    CdrEvidence evid;
    CdrLink link;
    CdrFob fob;

    uint32_t t0; /* tick the session began */
    uint32_t hz; /* kernel tick frequency */

    uint8_t band;
    uint8_t hop_start; /* rotates, so one loud band cannot always win the pass */
    bool camped;
    uint32_t camp_started_ms;
    uint32_t last_burst_ms;

    uint32_t link_at_ms; /* last time a held band was seen, 0 for never */

    uint8_t raster[CDR_RASTER_LEN];
    uint8_t raster_head;
    uint32_t raster_slot; /* slot index the head represents */

    int16_t peak[CdrBandCount];
    uint16_t hits[CdrBandCount];
    uint32_t bursts;
    uint32_t sweeps;
    uint32_t last_seq;
    CdrBurst last;
    bool have_last;

    uint8_t peak_score;
    uint8_t peak_level;
    uint32_t peak_at_ms;
    uint8_t peak_band;

    /* measured sample rate */
    uint32_t rate_mark;
    uint32_t rate_count;
    uint16_t sample_hz;
} CdrWork;

static void cdr_work_clear_session(CdrWork* w) {
    cdr_evid_init(&w->evid);
    cdr_link_init(&w->link);
    memset(w->raster, 0, sizeof(w->raster));
    w->raster_head = 0;
    w->raster_slot = 0;
    for(uint8_t i = 0; i < CdrBandCount; i++) {
        w->peak[i] = CDR_DBM_INVALID;
        w->hits[i] = 0;
    }
    w->bursts = 0;
    w->sweeps = 0;
    w->last_seq = 0;
    w->have_last = false;
    w->peak_score = 0;
    w->peak_level = CdrLevelQuiet;
    w->peak_at_ms = 0;
    w->peak_band = CdrBand433;
    w->link_at_ms = 0;
    w->last_burst_ms = 0;
    w->t0 = furi_get_tick();
}

static void cdr_work_init(CdrWork* w) {
    memset(w, 0, sizeof(*w));
    w->hz = furi_kernel_get_tick_frequency();
    if(w->hz == 0) w->hz = 1000;
    for(uint8_t i = 0; i < CdrBandCount; i++) cdr_floor_init(&w->floors[i], CDR_DBM_INVALID);
    cdr_bdet_init(&w->bdet, CdrBand433);
    w->sample_hz = 1000;
    cdr_work_clear_session(w);
    w->rate_mark = w->t0;
}

/* Walk the raster head forward to the slot @p now_ms belongs to, blanking the
 * columns that were skipped so a quiet minute reads as a quiet minute. */
static void cdr_raster_advance(CdrWork* w, uint32_t now_ms) {
    uint32_t slot = now_ms / CDR_RASTER_SLOT_MS;
    if(slot <= w->raster_slot) return;

    uint32_t steps = slot - w->raster_slot;
    if(steps > CDR_RASTER_LEN) steps = CDR_RASTER_LEN;
    for(uint32_t i = 0; i < steps; i++) {
        w->raster_head = (uint8_t)((w->raster_head + 1) % CDR_RASTER_LEN);
        w->raster[w->raster_head] = 0;
    }
    w->raster_slot = slot;
}

/* One burst, fully accounted for. */
static void cdr_work_take_burst(CdrWork* w, const CdrBurst* b, CdrRadioMode mode, uint32_t now_ms) {
    int16_t margin = (int16_t)(b->peak_dbm - b->floor_dbm);

    /* Enrolling a key means holding it against the Flipper. Anything faint is
     * somebody else's car, and adopting it would poison every later reading. */
    if(mode == CdrModeLearn && margin < CDR_LEARN_MIN_DB) return;

    if(b->band < CdrBandCount) w->hits[b->band]++;
    w->bursts++;
    w->last = *b;
    w->have_last = true;
    w->last_seq++;
    w->last_burst_ms = now_ms;

    uint8_t level = cdr_raster_level(b);
    if(level > w->raster[w->raster_head]) w->raster[w->raster_head] = level;

    if(mode == CdrModeGuard) cdr_evid_push(&w->evid, b);
}

/* ------------------------------------------------------------------ *
 * Sampling
 * ------------------------------------------------------------------ */

/* Listen on the current band for @p slice_ms. Returns the strongest sample
 * seen, or CDR_DBM_INVALID. Bursts fall out into the work state as they
 * complete. When @p extract is false only the floor and the peak are updated --
 * that is the hop dwell, where a fragment of a frame is worse than no frame. */
static int16_t cdr_listen(
    CdrRadio* r,
    CdrWork* w,
    const SubGhzDevice* device,
    uint32_t slice_ms,
    bool extract,
    CdrRadioMode mode,
    uint8_t enter_db) {
    uint32_t started = cdr_ms_since(w->t0, w->hz);
    int16_t max = CDR_DBM_INVALID;
    CdrBurst burst;
    uint8_t band = w->band;

    for(;;) {
        uint32_t now = cdr_ms_since(w->t0, w->hz);
        if(now - started >= slice_ms) break;
        if(!r->running) break;

        int16_t rssi = (int16_t)subghz_devices_get_rssi(device);
        if(max == CDR_DBM_INVALID || rssi > max) max = rssi;
        if(w->peak[band] == CDR_DBM_INVALID || rssi > w->peak[band]) w->peak[band] = rssi;

        cdr_floor_push(&w->floors[band], rssi);
        int16_t floor = w->floors[band].floor;

        if(extract) {
            cdr_link_push(&w->link, (int16_t)(rssi - floor) >= (int16_t)enter_db, 1);
            if(cdr_bdet_push(&w->bdet, rssi, floor, now, enter_db, &burst)) {
                cdr_work_take_burst(w, &burst, mode, now);
            }
        }

        w->rate_count++;
        furi_delay_us(CDR_SAMPLE_US);
    }

    /* Measured, not assumed: the burst lengths this app prints are only as
     * honest as the clock they were measured against. */
    uint32_t elapsed = furi_get_tick() - w->rate_mark;
    if(elapsed >= w->hz) {
        uint32_t rate = (w->rate_count * w->hz) / elapsed;
        w->sample_hz = (uint16_t)(rate > 0xFFFFu ? 0xFFFFu : rate);
        w->rate_mark = furi_get_tick();
        w->rate_count = 0;
    }

    return max;
}

/* ------------------------------------------------------------------ *
 * The thread
 * ------------------------------------------------------------------ */

static void cdr_leave_camp(CdrWork* w, uint32_t now_ms, CdrRadioMode mode) {
    CdrBurst burst;
    if(cdr_bdet_flush(&w->bdet, now_ms, &burst)) {
        cdr_work_take_burst(w, &burst, mode, now_ms);
    }
    w->camped = false;
}

static void cdr_enter_camp(CdrWork* w, uint8_t band, uint32_t now_ms) {
    w->band = band;
    w->camped = true;
    w->camp_started_ms = now_ms;
    cdr_bdet_init(&w->bdet, band);
    cdr_link_init(&w->link);
}

static int32_t cdr_radio_thread(void* ctx) {
    CdrRadio* r = ctx;
    CdrWork w;
    cdr_work_init(&w);

    subghz_devices_init();
    const SubGhzDevice* device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    if(!device) {
        FURI_LOG_E(TAG, "no internal CC1101");
        furi_mutex_acquire(r->mutex, FuriWaitForever);
        r->snap.radio_ok = false;
        furi_mutex_release(r->mutex);
        subghz_devices_deinit();
        return 0;
    }

    subghz_devices_begin(device);
    subghz_devices_reset(device);
    subghz_devices_load_preset(device, CDR_PRESET, NULL);

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->snap.radio_ok = true;
    CdrRadioMode mode = r->mode;
    uint8_t mask = r->req_band_mask;
    uint8_t sens = r->req_sens;
    uint8_t pin = r->req_pin;
    bool armed = r->req_armed;
    w.fob = r->req_fob;
    furi_mutex_release(r->mutex);

    if(mask == 0) mask = (uint8_t)((1u << CdrBandCount) - 1u);
    w.band = cdr_first_band(mask);
    cdr_tune(device, w.band);

    while(r->running) {
        /* ---- requests ---- */
        furi_mutex_acquire(r->mutex, FuriWaitForever);
        mode = r->mode;
        mask = r->req_band_mask ? r->req_band_mask : (uint8_t)((1u << CdrBandCount) - 1u);
        sens = r->req_sens;
        armed = r->req_armed;
        w.fob = r->req_fob;
        bool want_reset = r->req_reset;
        uint8_t want_pin = r->req_pin;
        r->req_reset = false;
        furi_mutex_release(r->mutex);

        if(want_reset) {
            cdr_work_clear_session(&w);
            cdr_bdet_init(&w.bdet, w.band);
            w.rate_mark = furi_get_tick();
            w.rate_count = 0;
        }

        uint8_t enter_db = mode == CdrModeLearn ? CDR_LEARN_MIN_DB - 5 : cdr_sens_enter_db(sens);
        uint32_t now = cdr_ms_since(w.t0, w.hz);

        /* ---- pinning ---- */
        if(want_pin != pin) {
            pin = want_pin;
            if(pin < CdrBandCount) {
                if(w.camped) cdr_leave_camp(&w, now, mode);
                if(cdr_tune(device, pin)) cdr_enter_camp(&w, pin, now);
            } else if(w.camped) {
                cdr_leave_camp(&w, now, mode);
            }
        }
        if(pin < CdrBandCount && !w.camped) {
            if(cdr_tune(device, pin)) cdr_enter_camp(&w, pin, now);
        }

        cdr_raster_advance(&w, now);

        /* ---- listen ---- */
        if(w.camped) {
            cdr_listen(r, &w, device, CDR_CAMP_SLICE_MS, true, mode, enter_db);
            now = cdr_ms_since(w.t0, w.hz);

            if(pin >= CdrBandCount) {
                bool idle = (now - w.last_burst_ms) > CDR_CAMP_IDLE_MS;
                /* A band being held open is the one thing more worth watching
                 * than a band producing frames, so occupancy defeats the idle
                 * timer -- otherwise the camp would end at 1.5 s and a held
                 * band could never accumulate the 3 s it takes to be called
                 * one. */
                if(w.link.occupancy_pct >= CDR_LINK_OCCUPANCY_PCT) idle = false;
                if(idle || (now - w.camp_started_ms) > CDR_CAMP_MAX_MS) {
                    cdr_leave_camp(&w, now, mode);
                }
            }
            if(w.link.active) w.link_at_ms = now;
        } else {
            for(uint8_t k = 0; k < CdrBandCount && r->running; k++) {
                uint8_t b = (uint8_t)((w.hop_start + k) % CdrBandCount);
                if(!(mask & (uint8_t)(1u << b))) continue;
                if(!cdr_tune(device, b)) continue;

                w.band = b;
                int16_t mx = cdr_listen(r, &w, device, CDR_HOP_DWELL_MS, false, mode, enter_db);

                if(mx != CDR_DBM_INVALID && w.floors[b].seeded &&
                   (int16_t)(mx - w.floors[b].floor) >= (int16_t)enter_db) {
                    cdr_enter_camp(&w, b, cdr_ms_since(w.t0, w.hz));
                    break;
                }
            }
            w.hop_start = (uint8_t)((w.hop_start + 1) % CdrBandCount);
            w.sweeps++;
        }

        /* ---- score ---- */
        now = cdr_ms_since(w.t0, w.hz);
        cdr_evid_expire(&w.evid, now, CDR_WINDOW_MS);

        CdrVerdict verdict;
        if(mode == CdrModeGuard) {
            cdr_score(&w.evid, w.fob.learned ? &w.fob : NULL, armed, sens, now, &verdict);
        } else {
            memset(&verdict, 0, sizeof(verdict));
            verdict.level = CdrLevelQuiet;
            verdict.jitter_pct = CDR_JITTER_NA;
            verdict.clone_pct = CDR_PCT_NA;
            verdict.band = w.band;
        }

        if(verdict.score > w.peak_score) {
            w.peak_score = verdict.score;
            w.peak_level = verdict.level;
            w.peak_at_ms = now;
            w.peak_band = verdict.band;
        }

        /* ---- publish ---- */
        furi_mutex_acquire(r->mutex, FuriWaitForever);
        CdrSnapshot* s = &r->snap;
        s->band = w.band;
        s->camped = w.camped;
        s->rssi = w.floors[w.band].seeded ? w.peak[w.band] : CDR_DBM_INVALID;
        s->sweeps = w.sweeps;
        for(uint8_t i = 0; i < CdrBandCount; i++) {
            s->floor[i] = w.floors[i].seeded ? w.floors[i].floor : CDR_DBM_INVALID;
            s->peak[i] = w.peak[i];
            s->hits[i] = w.hits[i];
        }
        s->verdict = verdict;
        s->link = w.link;
        s->link.active =
            w.link.active || (w.link_at_ms != 0 && (now - w.link_at_ms) < CDR_LINK_HOLD_MS);
        s->elapsed_ms = now;
        s->bursts = w.bursts;
        s->peak_score = w.peak_score;
        s->peak_level = w.peak_level;
        s->peak_at_ms = w.peak_at_ms;
        s->peak_band = w.peak_band;
        s->last = w.last;
        s->have_last = w.have_last;
        s->last_seq = w.last_seq;
        memcpy(s->raster, w.raster, sizeof(s->raster));
        s->raster_head = w.raster_head;
        s->sample_hz = w.sample_hz;
        furi_mutex_release(r->mutex);
    }

    subghz_devices_idle(device);
    subghz_devices_sleep(device);
    subghz_devices_end(device);
    subghz_devices_deinit();
    return 0;
}

/* ------------------------------------------------------------------ *
 * API
 * ------------------------------------------------------------------ */

CdrRadio* cdr_radio_alloc(void) {
    CdrRadio* r = malloc(sizeof(CdrRadio));
    memset(r, 0, sizeof(CdrRadio));
    r->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    r->req_band_mask = (uint8_t)((1u << CdrBandCount) - 1u);
    r->req_sens = CdrSensNormal;
    r->req_pin = CdrBandCount;
    r->snap.verdict.jitter_pct = CDR_JITTER_NA;
    r->snap.verdict.clone_pct = CDR_PCT_NA;
    for(uint8_t i = 0; i < CdrBandCount; i++) {
        r->snap.floor[i] = CDR_DBM_INVALID;
        r->snap.peak[i] = CDR_DBM_INVALID;
    }
    r->snap.rssi = CDR_DBM_INVALID;
    return r;
}

void cdr_radio_free(CdrRadio* r) {
    furi_assert(r);
    cdr_radio_stop(r);
    furi_mutex_free(r->mutex);
    free(r);
}

void cdr_radio_start(CdrRadio* r, CdrRadioMode mode, uint8_t band_mask, uint8_t sens) {
    furi_assert(r);
    if(r->running) cdr_radio_stop(r);

    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->mode = mode;
    r->req_band_mask = band_mask ? band_mask : (uint8_t)((1u << CdrBandCount) - 1u);
    r->req_sens = sens < CdrSensCount ? sens : CdrSensNormal;
    r->req_pin = CdrBandCount;
    r->req_reset = false;
    memset(&r->snap.raster, 0, sizeof(r->snap.raster));
    r->snap.raster_head = 0;
    r->snap.bursts = 0;
    r->snap.last_seq = 0;
    r->snap.have_last = false;
    r->snap.peak_score = 0;
    r->snap.peak_level = CdrLevelQuiet;
    r->snap.elapsed_ms = 0;
    furi_mutex_release(r->mutex);

    r->running = true;
    r->thread = furi_thread_alloc_ex("CardeaRadio", CDR_WORKER_STACK, cdr_radio_thread, r);
    furi_thread_start(r->thread);
}

void cdr_radio_stop(CdrRadio* r) {
    furi_assert(r);
    if(!r->running) return;
    r->running = false;
    furi_thread_join(r->thread);
    furi_thread_free(r->thread);
    r->thread = NULL;
}

bool cdr_radio_running(CdrRadio* r) {
    furi_assert(r);
    return r->running;
}

void cdr_radio_get(CdrRadio* r, CdrSnapshot* out) {
    furi_assert(r);
    furi_assert(out);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    *out = r->snap;
    furi_mutex_release(r->mutex);
}

void cdr_radio_set_armed(CdrRadio* r, bool armed) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_armed = armed;
    furi_mutex_release(r->mutex);
}

void cdr_radio_set_fob(CdrRadio* r, const CdrFob* fob) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    if(fob) {
        r->req_fob = *fob;
    } else {
        memset(&r->req_fob, 0, sizeof(r->req_fob));
    }
    furi_mutex_release(r->mutex);
}

void cdr_radio_set_sens(CdrRadio* r, uint8_t sens) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_sens = sens < CdrSensCount ? sens : CdrSensNormal;
    furi_mutex_release(r->mutex);
}

void cdr_radio_set_bands(CdrRadio* r, uint8_t band_mask) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_band_mask = band_mask ? band_mask : (uint8_t)((1u << CdrBandCount) - 1u);
    furi_mutex_release(r->mutex);
}

void cdr_radio_reset(CdrRadio* r) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_reset = true;
    furi_mutex_release(r->mutex);
}

void cdr_radio_pin(CdrRadio* r, uint8_t band) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    r->req_pin = band <= CdrBandCount ? band : CdrBandCount;
    furi_mutex_release(r->mutex);
}

uint8_t cdr_radio_pinned(CdrRadio* r) {
    furi_assert(r);
    furi_mutex_acquire(r->mutex, FuriWaitForever);
    uint8_t pin = r->req_pin;
    furi_mutex_release(r->mutex);
    return pin;
}
