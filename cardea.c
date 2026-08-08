#include "cardea_i.h"

#include <stdio.h>
#include <string.h>

/* ---------------- feedback ----------------
 *
 * A watchdog is usually not being looked at. Everything below is designed to be
 * understood from a pocket: the alarm is the only sequence that is loud, long
 * and red, and nothing else in the app is allowed to sound like it.
 *
 * Sequences of different lengths cannot share an array -- the pointer types do
 * not match -- so the selection below is written out rather than indexed.
 */

/* Something worth a glance. One low note, easy to sleep through on purpose. */
static const NotificationSequence seq_odd = {
    &message_note_c4,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

/* Worth looking at. Two rising notes and a blue flash. */
static const NotificationSequence seq_suspicious = {
    &message_blue_255,
    &message_note_e4,
    &message_delay_50,
    &message_note_a4,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_blue_0,
    NULL,
};

/* The alarm. Rising three-note siren, red, and a shove in the pocket. */
static const NotificationSequence seq_alarm = {
    &message_red_255,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_note_a5,
    &message_delay_250,
    &message_sound_off,
    &message_vibro_off,
    &message_delay_100,
    &message_red_0,
    NULL,
};

/* Same alarm with the speaker and motor stripped out, for silent watch. */
static const NotificationSequence seq_alarm_quiet = {
    &message_red_255,
    &message_delay_250,
    &message_red_0,
    &message_delay_100,
    &message_red_255,
    &message_delay_250,
    &message_red_0,
    NULL,
};

static const NotificationSequence seq_arm = {
    &message_note_a4,
    &message_delay_50,
    &message_note_e5,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_disarm = {
    &message_note_e5,
    &message_delay_50,
    &message_note_a4,
    &message_delay_50,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_capture = {
    &message_green_255,
    &message_note_e5,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_green_0,
    NULL,
};

static const NotificationSequence seq_saved = {
    &message_green_255,
    &message_note_c5,
    &message_delay_50,
    &message_note_e5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_100,
    &message_sound_off,
    &message_green_0,
    NULL,
};

/* The "still awake" blink. No sound, ever -- this one fires all night. */
static const NotificationSequence seq_heartbeat = {
    &message_blue_255,
    &message_delay_10,
    &message_blue_0,
    NULL,
};

void cardea_notify_level(CardeaApp* app, uint8_t level) {
    furi_assert(app);
    if(app->muted) return;

    if(level == CdrLevelOdd) {
        if(app->settings.sound) notification_message(app->notifications, &seq_odd);
    } else if(level == CdrLevelSuspicious) {
        if(app->settings.sound) {
            notification_message(app->notifications, &seq_suspicious);
        } else if(app->settings.led) {
            notification_message(app->notifications, &seq_heartbeat);
        }
    }
}

void cardea_notify_alarm(CardeaApp* app) {
    furi_assert(app);
    /* Muting silences the speaker and the motor. It does not silence the LED:
     * the whole point of a mute is to keep watching discreetly, not to stop
     * watching. */
    bool loud = !app->muted && app->settings.sound;
    if(loud) {
        notification_message(app->notifications, &seq_alarm);
    } else if(app->settings.led) {
        notification_message(app->notifications, &seq_alarm_quiet);
    }
    if(!app->muted && app->settings.vibro && !app->settings.sound) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

void cardea_notify_arm(CardeaApp* app, bool armed) {
    furi_assert(app);
    if(app->muted || !app->settings.sound) return;
    notification_message(app->notifications, armed ? &seq_arm : &seq_disarm);
}

void cardea_notify_capture(CardeaApp* app) {
    furi_assert(app);
    if(app->settings.sound) notification_message(app->notifications, &seq_capture);
}

void cardea_notify_saved(CardeaApp* app) {
    furi_assert(app);
    if(app->settings.sound) notification_message(app->notifications, &seq_saved);
}

void cardea_notify_heartbeat(CardeaApp* app) {
    furi_assert(app);
    if(!app->settings.led) return;
    notification_message(app->notifications, &seq_heartbeat);
}

/* ---------------- formatting ---------------- */

void cardea_format_elapsed(char* out, size_t len, uint32_t ms) {
    uint32_t s = ms / 1000u;
    uint32_t h = s / 3600u;
    s -= h * 3600u;
    uint32_t m = s / 60u;
    s -= m * 60u;

    /* Clamped where the compiler can see it: -Werror=format-truncation will
     * not accept a width it cannot prove. */
    if(h > 99u) h = 99u;
    if(m > 59u) m = 59u;
    if(s > 59u) s = 59u;

    if(h) {
        snprintf(out, len, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    } else {
        snprintf(out, len, "%02u:%02u", (unsigned)m, (unsigned)s);
    }
}

/* ---------------- view dispatcher plumbing ---------------- */

static bool cardea_custom_event_callback(void* context, uint32_t event) {
    CardeaApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool cardea_back_event_callback(void* context) {
    CardeaApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void cardea_tick_event_callback(void* context) {
    CardeaApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* ---------------- lifecycle ---------------- */

static CardeaApp* cardea_app_alloc(void) {
    CardeaApp* app = malloc(sizeof(CardeaApp));
    memset(app, 0, sizeof(CardeaApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&cardea_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, cardea_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, cardea_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, cardea_tick_event_callback, CARDEA_TICK_MS);

    cdr_store_defaults(&app->settings);
    cdr_store_load(&app->settings);

    app->radio = cdr_radio_alloc();
    app->snap.verdict.jitter_pct = CDR_JITTER_NA;
    app->snap.verdict.clone_pct = CDR_PCT_NA;

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewSettings, variable_item_list_get_view(app->var_item_list));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, CardeaViewAbout, widget_get_view(app->widget));

    app->splash_view = splash_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewSplash, splash_view_get_view(app->splash_view));

    app->guard_view = guard_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewGuard, guard_view_get_view(app->guard_view));

    app->learn_view = learn_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewLearn, learn_view_get_view(app->learn_view));

    app->report_view = report_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewReport, report_view_get_view(app->report_view));

    app->primer_view = primer_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CardeaViewPrimer, primer_view_get_view(app->primer_view));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void cardea_app_free(CardeaApp* app) {
    furi_assert(app);

    cdr_radio_stop(app->radio);
    cdr_store_save(&app->settings);

    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewAbout);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewSplash);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewGuard);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewLearn);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewReport);
    view_dispatcher_remove_view(app->view_dispatcher, CardeaViewPrimer);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    widget_free(app->widget);
    splash_view_free(app->splash_view);
    guard_view_free(app->guard_view);
    learn_view_free(app->learn_view);
    report_view_free(app->report_view);
    primer_view_free(app->primer_view);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    cdr_radio_free(app->radio);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t cardea_app(void* p) {
    UNUSED(p);
    CardeaApp* app = cardea_app_alloc();
    scene_manager_next_scene(app->scene_manager, CardeaSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    cardea_app_free(app);
    return 0;
}
