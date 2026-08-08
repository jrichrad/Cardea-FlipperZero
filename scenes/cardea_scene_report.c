#include "../cardea_i.h"

#include <string.h>

void cardea_scene_report_on_enter(void* context) {
    CardeaApp* app = context;

    ReportState st;
    memset(&st, 0, sizeof(st));
    st.snap = app->report;
    st.valid = app->have_report;
    st.fob_learned = app->settings.fob.learned;
    st.logged = app->settings.log_csv;

    report_view_set_state(app->report_view, &st);
    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewReport);
}

bool cardea_scene_report_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void cardea_scene_report_on_exit(void* context) {
    UNUSED(context);
}
