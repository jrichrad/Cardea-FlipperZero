#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "cardea_icons.h" /* generated from icons/ by fbt */

#include "helpers/cdr_detect.h"
#include "helpers/cdr_radio.h"
#include "helpers/cdr_store.h"
#include "views/splash_view.h"
#include "views/guard_view.h"
#include "views/learn_view.h"
#include "views/report_view.h"
#include "views/primer_view.h"
#include "scenes/cardea_scene.h"

#define CARDEA_VERSION "1.0"

/* GUI tick. Fast enough that the raster scrolls smoothly and the alert feels
 * immediate, slow enough that the GUI thread is not fighting the radio worker
 * for the SPI bus. */
#define CARDEA_TICK_MS 100

/* Two confirming presses before a key template is accepted. One reading can be
 * a reflection; two agreeing readings are a key. */
#define CARDEA_LEARN_SAMPLES 2

/* Once an alert has been dismissed, the same event must not immediately raise
 * it again -- the evidence window is twenty seconds long and will still be
 * full of the thing that just fired. */
#define CARDEA_ALERT_COOLDOWN_MS 20000u

/* How often the alarm repeats while the alert is on screen. */
#define CARDEA_ALERT_REPEAT_MS 2200u

/* A slow blink while armed, so a Flipper face-down on the passenger seat still
 * says "I am awake". */
#define CARDEA_HEARTBEAT_MS 5000u

typedef enum {
    CardeaViewSplash,
    CardeaViewSubmenu,
    CardeaViewGuard,
    CardeaViewLearn,
    CardeaViewReport,
    CardeaViewPrimer,
    CardeaViewSettings,
    CardeaViewAbout,
} CardeaViewId;

typedef enum {
    /* Above any submenu index, so a splash skip cannot be mistaken for a menu
     * selection. */
    CardeaEventSkipSplash = 100,
    CardeaEventArmToggle,
    CardeaEventMuteToggle,
    CardeaEventAlertDismiss,
    CardeaEventLearnSave,
    CardeaEventLearnRetry,
    CardeaEventLearnDone,
} CardeaCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    Widget* widget;

    SplashView* splash_view;
    GuardView* guard_view;
    LearnView* learn_view;
    ReportView* report_view;
    PrimerView* primer_view;

    CdrRadio* radio;
    CardeaSettings settings;

    /* Half a kilobyte. It lives here rather than on a scene's stack, because
     * the GUI thread's stack is not the place for it. */
    CdrSnapshot snap;

    /* --- guard session --- */
    bool armed;
    uint32_t arm_at_ms; /* elapsed time the arming delay expires, 0 = not pending */
    bool muted;
    uint8_t last_level; /* to spot the verdict climbing */
    uint32_t alert_until_ms; /* cooldown after a dismissal */
    uint32_t alert_last_ping;
    uint32_t heartbeat_last;
    bool alert_shown;
    bool have_report;
    CdrSnapshot report; /* frozen at the end of the last watch */

    /* --- learn session --- */
    uint8_t learn_got;
    uint32_t learn_seq; /* last burst sequence number consumed */
    CdrFob learn_fob;

    bool splash_done;
    uint8_t splash_ticks;
} CardeaApp;

/* Feedback. Every one of these is gated by the settings and by the mute
 * toggle, and defined in cardea.c. */
void cardea_notify_level(CardeaApp* app, uint8_t level);
void cardea_notify_alarm(CardeaApp* app);
void cardea_notify_arm(CardeaApp* app, bool armed);
void cardea_notify_capture(CardeaApp* app);
void cardea_notify_saved(CardeaApp* app);
void cardea_notify_heartbeat(CardeaApp* app);

/** Format an elapsed millisecond count as MM:SS, or HH:MM:SS past an hour. */
void cardea_format_elapsed(char* out, size_t len, uint32_t ms);
