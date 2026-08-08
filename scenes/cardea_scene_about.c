#include "../cardea_i.h"

#include <furi.h>

void cardea_scene_about_on_enter(void* context) {
    CardeaApp* app = context;
    FuriString* text = furi_string_alloc();

    furi_string_printf(
        text,
        "\e#Cardea %s\e#\n"
        "Relay attack watch for the\n"
        "Flipper Zero. Receive only --\n"
        "it never transmits.\n"
        "\n"
        "\e#Why it works\e#\n"
        "A keyless car asks its key a\n"
        "question over a 125 kHz field\n"
        "that reaches about a metre.\n"
        "The key answers over UHF, and\n"
        "that answer carries hundreds\n"
        "of metres.\n"
        "\n"
        "The usual theft relays only\n"
        "the 125 kHz question -- one\n"
        "box at the car, one by your\n"
        "front door. Nobody relays the\n"
        "answer, because the answer\n"
        "reaches the car by itself.\n"
        "\n"
        "So Cardea sits with the car\n"
        "and listens for a key that is\n"
        "answering when nobody asked.\n"
        "\n"
        "\e#What it cannot do\e#\n"
        "It cannot hear the 125 kHz\n"
        "half; no Flipper can, at that\n"
        "range. It cannot decrypt a\n"
        "key's reply, so it can never\n"
        "prove a burst came from your\n"
        "key. It reports evidence, and\n"
        "it stops at RELAY LIKELY.\n"
        "\n"
        "\e#Keys\e#\n"
        "OK        away / here\n"
        "Hold OK   mute the alarm\n"
        "Left/Right  change page\n"
        "Up/Down   pin one band\n"
        "Back      end the watch\n"
        "\n"
        "\e#Reading the screen\e#\n"
        "The strip is 30 s of bursts,\n"
        "height is strength over the\n"
        "noise floor. The dotted line\n"
        "is the level a burst has to\n"
        "clear to count at all.\n"
        "\n"
        "UNSOL   bursts nobody asked\n"
        "        for, while away\n"
        "RHYTHM  gaps too even to be\n"
        "        a human thumb\n"
        "CLONE   the same frame over\n"
        "        and over\n"
        "\n"
        "Two of the three must agree\n"
        "before it will say RELAY\n"
        "LIKELY, and one of the two\n"
        "must be RHYTHM -- pressing\n"
        "your own key six times is\n"
        "unsolicited and identical\n"
        "too. Only the metronome is\n"
        "something a person does not\n"
        "do.\n"
        "\n"
        "BEACON means the rhythm has\n"
        "been running for over a\n"
        "minute. That is a weather\n"
        "station, not a thief, and it\n"
        "stops counting.\n"
        "\n"
        "HELD means a band is being\n"
        "held open by a carrier. It is\n"
        "reported on its own, never\n"
        "folded into the score.\n"
        "\n"
        "\e#Log\e#\n"
        "%s\n"
        "\n"
        "\e#Legal\e#\n"
        "Passive monitoring only. Use\n"
        "it on your own vehicle and\n"
        "your own keys.\n"
        "\n"
        "MIT licensed.\n"
        "github.com/at0m-b0mb\n"
        "  /Cardea-FlipperZero\n",
        CARDEA_VERSION,
        cdr_log_path_pretty);

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);

    view_dispatcher_switch_to_view(app->view_dispatcher, CardeaViewAbout);
}

bool cardea_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void cardea_scene_about_on_exit(void* context) {
    CardeaApp* app = context;
    widget_reset(app->widget);
}
