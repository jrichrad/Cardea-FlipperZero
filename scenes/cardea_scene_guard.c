#include "../cardea_i.h"

#include <string.h>

/* Cycle the receiver through: hop, then each watched band, then hop again.
 * Bands the operator turned off are skipped, because offering to pin to a band
 * the app is not watching would be a lie. */
static uint8_t cardea_next_pin(const CardeaApp* app, uint8_t current, bool forward) {
    for(uint8_t step = 0; step < CdrBandCount + 1; step++) {
        if(forward) {
            current = (uint8_t)(current >= CdrBandCount ? 0 : current + 1);
        } else {
            current = (uint8_t)(current == 0 ? CdrBandCount : current - 1);
        }
        if(current >= CdrBandCount) return CdrBandCount; /* back to hopping */
        if(app->settings.band_mask & (uint8_t)(1u << current)) return current;
    }
    return CdrBandCount;
}

static void cardea_guard_arm(CardeaApp* app, bool armed) {
    app->armed = armed;
    app->arm_at_ms = 0;
    cdr_radio_set_armed(app->radio, armed);
    /* The walk to the front door is not evidence against the person who took
     * it, so arming starts the window over. */
    cdr_radio_reset(app->radio);
    app->last_level = CdrLevelQuiet;
    app->alert_shown = false;
    app->alert_until_ms = 0;
    app->heartbeat_last = 0;
    cardea_notify_arm(app, armed);
}

static void cardea_guard_cb(void* context, GuardEvent event) {
    CardeaApp* app = context;
    switch(event) {
    case GuardEventArm:
        view_dispatcher_send_custom_event(app->view_dispatcher, CardeaEventArmToggle);
        break;
    case GuardEventMute:
        view_dispatcher_send_custom_event(app->view_dispatcher, CardeaEventMuteToggle);
        break;
    case GuardEventDismiss:
        view_dispatcher_send_custom_event(app->view_dispatcher, CardeaEventAlertDismiss);
        break;
    case GuardEventPinNext:
        cdr_radio_pin(app->radio, cardea_next_pin(app, cdr_radio_pinned(app->radio), true));
        break;
    case GuardEventPinPrev:
        cdr_radio_pin(app->radio, cardea_next_pin(app, cdr_radio_pinned(app->radio), false));
        break;
    default:
        break;
    }
}

void cardea_scene_guard_on_enter(void* context) {
    CardeaApp* app = context;

    app->armed = false;
    app->muted = false;
    app->last_level = CdrLevelQuiet;
    app->alert_shown = false;
    app->alert_until_ms = 0;
    app->alert_last_ping = 0;
    app->heartbeat_last = 0;
    memset(&app->snap, 0, sizeof(app->snap));
    app->snap.radio_ok = true;
    app->snap.verdict.jitter_pct = CDR_JITTER_NA;
    app->snap.verdict.clone_pct = CDR_PCT_NA;
    for(uint8_t i = 0; i < CdrBandCount; i++) {
        app->snap.floor[i] = CDR_DBM_INVALID;
        app->snap.peak[i] = CDR_DBM_INVALID;
    }

    /* Arming after a delay is the normal case: you press Guard, put the
     * Flipper on the dashboard and walk off, and it starts counting your key
     * as unexplained only once you are gone. */
    app->arm_at_ms = (uint32_t)cdr_arm_delay_seconds[app->settings.arm_delay] * 1000u;

    guard_view_reset(app->guard_view);
    guard_view_set_callback(app->guard_view, cardea_guard_cb, app);

    cdr_radio_set_fob(app->radio, app->settings.fob.learned ? &app->settings.fob : NULL);
    cdr_radio_set_armed(app->radio, false);
    cdr_radio_start(app->radio, CdrModeGuard, app->settings.band_mask, app->settings.sens);

    if(app->settings.keep_awake) {
        notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewGuard);
}

static void cardea_guard_push_state(CardeaApp* app) {
    GuardState st;
    memset(&st, 0, sizeof(st));
    st.snap = app->snap;
    st.armed = app->armed;
    st.muted = app->muted;
    st.alert = app->alert_shown;
    st.fob_learned = app->settings.fob.learned;
    st.pinned = cdr_radio_pinned(app->radio);
    st.band_mask = app->settings.band_mask;
    st.sens = app->settings.sens;

    if(app->arm_at_ms > app->snap.elapsed_ms) {
        uint32_t left = (app->arm_at_ms - app->snap.elapsed_ms + 999u) / 1000u;
        st.arm_in_s = (uint16_t)(left > 999u ? 999u : left);
    }

    guard_view_set_state(app->guard_view, &st);
}

static void cardea_guard_tick(CardeaApp* app) {
    cdr_radio_get(app->radio, &app->snap);
    uint32_t now = app->snap.elapsed_ms;

    /* --- the arming countdown --- */
    if(app->arm_at_ms && now >= app->arm_at_ms) {
        cardea_guard_arm(app, true);
        now = 0; /* the reset restarted the session clock */
    }

    /* --- the verdict climbing --- */
    uint8_t level = app->snap.verdict.level;
    if(level > app->last_level) {
        bool loggable = level >= CdrLevelSuspicious;

        if(level >= CdrLevelLikely && !app->alert_shown && now >= app->alert_until_ms) {
            app->alert_shown = true;
            app->alert_last_ping = 0;
        } else if(!app->alert_shown) {
            cardea_notify_level(app, level);
        }

        if(loggable && app->settings.log_csv) cdr_store_log_event(&app->snap);
    }
    app->last_level = level;

    /* --- the alarm, while it is up --- */
    if(app->alert_shown) {
        if(app->alert_last_ping == 0 || (now - app->alert_last_ping) >= CARDEA_ALERT_REPEAT_MS) {
            cardea_notify_alarm(app);
            app->alert_last_ping = now ? now : 1;
        }
    } else if(app->armed) {
        /* "I am still awake", for a Flipper face down on a seat. */
        if((now - app->heartbeat_last) >= CARDEA_HEARTBEAT_MS) {
            app->heartbeat_last = now;
            cardea_notify_heartbeat(app);
        }
    }

    cardea_guard_push_state(app);
    guard_view_tick(app->guard_view);
}

bool cardea_scene_guard_on_event(void* context, SceneManagerEvent event) {
    CardeaApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        cardea_guard_tick(app);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case CardeaEventArmToggle:
            cardea_guard_arm(app, !app->armed);
            cardea_guard_push_state(app);
            consumed = true;
            break;

        case CardeaEventMuteToggle:
            app->muted = !app->muted;
            cardea_guard_push_state(app);
            consumed = true;
            break;

        case CardeaEventAlertDismiss:
            app->alert_shown = false;
            /* The evidence window is still full of whatever just fired, so a
             * cooldown is the difference between an alert and a stuck horn. */
            app->alert_until_ms = app->snap.elapsed_ms + CARDEA_ALERT_COOLDOWN_MS;
            if(app->settings.log_csv) cdr_store_log_event(&app->snap);
            cardea_guard_push_state(app);
            consumed = true;
            break;

        default:
            break;
        }
    }

    return consumed;
}

void cardea_scene_guard_on_exit(void* context) {
    CardeaApp* app = context;

    cdr_radio_get(app->radio, &app->snap);
    cdr_radio_stop(app->radio);
    cdr_radio_pin(app->radio, CdrBandCount);

    /* Freeze what the watch saw, so the report survives the radio being torn
     * down and the next watch being started. */
    app->report = app->snap;
    app->have_report = true;

    if(app->settings.keep_awake) {
        notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    }
}
