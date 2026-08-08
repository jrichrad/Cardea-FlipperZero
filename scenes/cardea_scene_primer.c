#include "../cardea_i.h"

void cardea_scene_primer_on_enter(void* context) {
    CardeaApp* app = context;
    primer_view_reset(app->primer_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewPrimer);
}

bool cardea_scene_primer_on_event(void* context, SceneManagerEvent event) {
    CardeaApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        primer_view_tick(app->primer_view);
        return true;
    }
    return false;
}

void cardea_scene_primer_on_exit(void* context) {
    UNUSED(context);
}
