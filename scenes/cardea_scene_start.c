#include "../cardea_i.h"

/* The intro runs about 1.8 s at the GUI tick, then hands off to the menu; any
 * key skips it. It lives inside the root scene rather than on the scene stack,
 * so coming back from a watch never replays it, and Back from the menu still
 * exits the app cleanly. */
#define CDR_SPLASH_TICKS 18

typedef enum {
    StartIndexGuard,
    StartIndexLearn,
    StartIndexReport,
    StartIndexPrimer,
    StartIndexSettings,
    StartIndexAbout,
} StartIndex;

static void cardea_scene_start_submenu_cb(void* context, uint32_t index) {
    CardeaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void cardea_scene_start_show_menu(CardeaApp* app) {
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Cardea");
    submenu_add_item(
        submenu, "Guard - watch the car", StartIndexGuard, cardea_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu,
        app->settings.fob.learned ? "Re-learn my key" : "Learn my key",
        StartIndexLearn,
        cardea_scene_start_submenu_cb,
        app);
    submenu_add_item(
        submenu, "Last watch report", StartIndexReport, cardea_scene_start_submenu_cb, app);
    submenu_add_item(
        submenu, "How relay theft works", StartIndexPrimer, cardea_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "Settings", StartIndexSettings, cardea_scene_start_submenu_cb, app);
    submenu_add_item(submenu, "About", StartIndexAbout, cardea_scene_start_submenu_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, CardeaSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewSubmenu);
}

static void cardea_scene_start_skip_splash(void* context) {
    CardeaApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CardeaEventSkipSplash);
}

void cardea_scene_start_on_enter(void* context) {
    CardeaApp* app = context;

    if(!app->splash_done) {
        app->splash_ticks = 0;
        splash_view_set_progress(app->splash_view, 0);
        splash_view_set_skip_callback(app->splash_view, cardea_scene_start_skip_splash, app);
        view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewSplash);
    } else {
        cardea_scene_start_show_menu(app);
    }
}

bool cardea_scene_start_on_event(void* context, SceneManagerEvent event) {
    CardeaApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(!app->splash_done) {
            app->splash_ticks++;
            uint8_t progress = (uint8_t)((app->splash_ticks * 100u) / CDR_SPLASH_TICKS);
            splash_view_set_progress(app->splash_view, progress);
            if(app->splash_ticks >= CDR_SPLASH_TICKS) {
                app->splash_done = true;
                cardea_scene_start_show_menu(app);
            }
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(!app->splash_done && event.event == CardeaEventSkipSplash) {
            app->splash_done = true;
            cardea_scene_start_show_menu(app);
            return true;
        }

        scene_manager_set_scene_state(app->scene_manager, CardeaSceneStart, event.event);
        switch(event.event) {
        case StartIndexGuard:
            scene_manager_next_scene(app->scene_manager, CardeaSceneGuard);
            consumed = true;
            break;
        case StartIndexLearn:
            scene_manager_next_scene(app->scene_manager, CardeaSceneLearn);
            consumed = true;
            break;
        case StartIndexReport:
            scene_manager_next_scene(app->scene_manager, CardeaSceneReport);
            consumed = true;
            break;
        case StartIndexPrimer:
            scene_manager_next_scene(app->scene_manager, CardeaScenePrimer);
            consumed = true;
            break;
        case StartIndexSettings:
            scene_manager_next_scene(app->scene_manager, CardeaSceneSettings);
            consumed = true;
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, CardeaSceneAbout);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void cardea_scene_start_on_exit(void* context) {
    CardeaApp* app = context;
    submenu_reset(app->submenu);
}
