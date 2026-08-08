#include "../cardea_i.h"

#include <string.h>

/* How far a confirming capture may differ from the first and still be called
 * the same key, in tenths of the first one's length. */
#define LEARN_DUR_LO 6
#define LEARN_DUR_HI 15

static void cardea_learn_cb(void* context, LearnEvent event) {
    CardeaApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, event == LearnEventSave ? CardeaEventLearnSave : CardeaEventLearnRetry);
}

static void cardea_learn_push_state(CardeaApp* app) {
    LearnState st;
    memset(&st, 0, sizeof(st));
    st.got = app->learn_got;
    st.need = CARDEA_LEARN_SAMPLES;
    st.fob = app->learn_fob;
    st.last = app->snap.last;
    st.have_last = app->snap.have_last;
    st.radio_ok = app->snap.radio_ok;
    learn_view_set_state(app->learn_view, &st);
}

static void cardea_learn_begin(CardeaApp* app) {
    app->learn_got = 0;
    memset(&app->learn_fob, 0, sizeof(app->learn_fob));
    app->learn_seq = app->snap.last_seq;
}

/* Does this capture agree with the one already held? */
static bool cardea_learn_agrees(const CdrFob* have, const CdrBurst* b) {
    if(b->band != have->band) return false;
    uint32_t lo = ((uint32_t)have->dur_ms * LEARN_DUR_LO) / 10u;
    uint32_t hi = ((uint32_t)have->dur_ms * LEARN_DUR_HI) / 10u;
    if(b->dur_ms < lo || b->dur_ms > hi) return false;
    return cdr_env_distance(b->env, have->env) <= CDR_ENV_TOL;
}

static void cardea_learn_take(CardeaApp* app, const CdrBurst* b) {
    if(app->learn_got == 0 || !cardea_learn_agrees(&app->learn_fob, b)) {
        /* Either the first capture, or one that disagrees with it. A
         * disagreement is not an error -- it usually means the first reading
         * caught something else in the street -- so this becomes the new
         * first, rather than being rejected with a complaint. */
        memset(&app->learn_fob, 0, sizeof(app->learn_fob));
        app->learn_fob.band = b->band;
        app->learn_fob.dur_ms = b->dur_ms;
        app->learn_fob.peak_dbm = b->peak_dbm;
        memcpy(app->learn_fob.env, b->env, CDR_ENV_BINS);
        app->learn_got = 1;
    } else {
        /* Agreeing captures are averaged: two readings of one frame differ
         * only by noise, and the mean of them has less of it. */
        app->learn_fob.dur_ms = (uint16_t)(((uint32_t)app->learn_fob.dur_ms + b->dur_ms) / 2u);
        if(b->peak_dbm > app->learn_fob.peak_dbm) app->learn_fob.peak_dbm = b->peak_dbm;
        for(uint8_t i = 0; i < CDR_ENV_BINS; i++) {
            app->learn_fob.env[i] =
                (uint8_t)(((uint16_t)app->learn_fob.env[i] + b->env[i]) / 2u);
        }
        app->learn_got++;
    }

    if(app->learn_got >= CARDEA_LEARN_SAMPLES) app->learn_fob.learned = true;
    cardea_notify_capture(app);
}

void cardea_scene_learn_on_enter(void* context) {
    CardeaApp* app = context;

    memset(&app->snap, 0, sizeof(app->snap));
    app->snap.radio_ok = true;
    cardea_learn_begin(app);

    learn_view_set_callback(app->learn_view, cardea_learn_cb, app);
    cardea_learn_push_state(app);

    /* Learn mode listens on every band regardless of the watch list: the whole
     * point is to find out which one this key uses. */
    cdr_radio_set_fob(app->radio, NULL);
    cdr_radio_start(app->radio, CdrModeLearn, 0, app->settings.sens);

    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewLearn);
}

bool cardea_scene_learn_on_event(void* context, SceneManagerEvent event) {
    CardeaApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        cdr_radio_get(app->radio, &app->snap);

        if(app->learn_got < CARDEA_LEARN_SAMPLES && app->snap.have_last &&
           app->snap.last_seq != app->learn_seq) {
            app->learn_seq = app->snap.last_seq;
            cardea_learn_take(app, &app->snap.last);
        }

        cardea_learn_push_state(app);
        learn_view_tick(app->learn_view);
        consumed = true;

    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == CardeaEventLearnSave) {
            app->settings.fob = app->learn_fob;
            app->settings.fob.learned = true;
            cdr_store_save(&app->settings);
            cardea_notify_saved(app);
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        } else if(event.event == CardeaEventLearnRetry) {
            cardea_learn_begin(app);
            cardea_learn_push_state(app);
            consumed = true;
        }
    }

    return consumed;
}

void cardea_scene_learn_on_exit(void* context) {
    CardeaApp* app = context;
    cdr_radio_stop(app->radio);
}
