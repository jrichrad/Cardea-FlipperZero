#include "../cardea_i.h"

static const char* const on_off[2] = {"Off", "On"};

/* Band toggles come first: they are the setting most likely to be wrong for a
 * given car, and the one with the most effect on how often the receiver is
 * pointed at the right place. */
static void cardea_settings_band_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);

    /* A change callback only ever fires for the selected row, and the five
     * band rows are the first five in the list, so the row index is the band. */
    uint8_t band = variable_item_list_get_selected_item_index(app->var_item_list);
    if(band >= CdrBandCount) return;

    uint8_t mask = app->settings.band_mask;
    if(idx) {
        mask |= (uint8_t)(1u << band);
    } else {
        mask &= (uint8_t)~(1u << band);
    }

    /* A watchdog watching nothing is worse than no watchdog, because it still
     * looks like one. The last band cannot be switched off. */
    if(mask == 0) {
        variable_item_set_current_value_index(item, 1);
        variable_item_set_current_value_text(item, on_off[1]);
        return;
    }

    app->settings.band_mask = mask;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void cardea_settings_sens_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.sens = idx;
    variable_item_set_current_value_text(item, cdr_sens_labels[idx]);
}

static void cardea_settings_delay_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.arm_delay = idx;
    variable_item_set_current_value_text(item, cdr_arm_delay_labels[idx]);
}

static void cardea_settings_sound_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.sound = idx != 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void cardea_settings_vibro_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.vibro = idx != 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void cardea_settings_led_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.led = idx != 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void cardea_settings_log_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.log_csv = idx != 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void cardea_settings_awake_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.keep_awake = idx != 0;
    variable_item_set_current_value_text(item, on_off[idx]);
}

static void cardea_settings_forget_cb(VariableItem* item) {
    CardeaApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    if(idx == 0) {
        memset(&app->settings.fob, 0, sizeof(app->settings.fob));
    }
    variable_item_set_current_value_text(item, app->settings.fob.learned ? "Learned" : "None");
}

void cardea_scene_settings_on_enter(void* context) {
    CardeaApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    for(uint8_t i = 0; i < CdrBandCount; i++) {
        bool on = (app->settings.band_mask & (uint8_t)(1u << i)) != 0;
        item = variable_item_list_add(list, cdr_bands[i].menu, 2, cardea_settings_band_cb, app);
        variable_item_set_current_value_index(item, on ? 1 : 0);
        variable_item_set_current_value_text(item, on_off[on ? 1 : 0]);
    }

    item = variable_item_list_add(list, "Sensitivity", CdrSensCount, cardea_settings_sens_cb, app);
    variable_item_set_current_value_index(item, app->settings.sens);
    variable_item_set_current_value_text(item, cdr_sens_labels[app->settings.sens]);

    item = variable_item_list_add(
        list, "Arm after", CDR_ARM_DELAY_COUNT, cardea_settings_delay_cb, app);
    variable_item_set_current_value_index(item, app->settings.arm_delay);
    variable_item_set_current_value_text(item, cdr_arm_delay_labels[app->settings.arm_delay]);

    item = variable_item_list_add(list, "Sound", 2, cardea_settings_sound_cb, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(list, "Vibrate", 2, cardea_settings_vibro_cb, app);
    variable_item_set_current_value_index(item, app->settings.vibro ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.vibro ? 1 : 0]);

    item = variable_item_list_add(list, "LED", 2, cardea_settings_led_cb, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.led ? 1 : 0]);

    item = variable_item_list_add(list, "Log to SD", 2, cardea_settings_log_cb, app);
    variable_item_set_current_value_index(item, app->settings.log_csv ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.log_csv ? 1 : 0]);

    item = variable_item_list_add(list, "Screen on", 2, cardea_settings_awake_cb, app);
    variable_item_set_current_value_index(item, app->settings.keep_awake ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->settings.keep_awake ? 1 : 0]);

    item = variable_item_list_add(list, "My key", 2, cardea_settings_forget_cb, app);
    variable_item_set_current_value_index(item, app->settings.fob.learned ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.fob.learned ? "Learned" : "None");

    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, CardeaSceneSettings));

    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewSettings);
}

bool cardea_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void cardea_scene_settings_on_exit(void* context) {
    CardeaApp* app = context;
    scene_manager_set_scene_state(
        app->scene_manager,
        CardeaSceneSettings,
        variable_item_list_get_selected_item_index(app->var_item_list));
    variable_item_list_reset(app->var_item_list);
    cdr_store_save(&app->settings);
}
