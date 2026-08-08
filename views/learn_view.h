/* Enrolling the key you are guarding.
 *
 * Optional, and the single biggest improvement available to the detector.
 * Without it "a key-shaped burst" means anything between 4 and 150 ms; with it
 * the app is looking for one length, one shape and one band, and every other
 * remote in the street stops counting.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/cdr_detect.h"

typedef struct LearnView LearnView;

typedef enum {
    LearnEventSave,
    LearnEventRetry,
} LearnEvent;

typedef void (*LearnCallback)(void* context, LearnEvent event);

typedef struct {
    uint8_t got; /* confirming captures so far */
    uint8_t need;
    CdrFob fob; /* the template as it stands */
    CdrBurst last; /* the most recent capture */
    bool have_last;
    bool radio_ok;
} LearnState;

LearnView* learn_view_alloc(void);
void learn_view_free(LearnView* v);
View* learn_view_get_view(LearnView* v);

void learn_view_set_callback(LearnView* v, LearnCallback cb, void* context);
void learn_view_set_state(LearnView* v, const LearnState* state);
void learn_view_tick(LearnView* v);
