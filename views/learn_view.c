#include "learn_view.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

struct LearnView {
    View* view;
    LearnCallback cb;
    void* context;
};

typedef struct {
    LearnState st;
    uint8_t anim;
} LearnModel;

static void draw_str_center(Canvas* c, int32_t cx, int32_t y, const char* s) {
    canvas_draw_str(c, cx - (int32_t)canvas_string_width(c, s) / 2, y, s);
}

static void learn_draw_title(Canvas* c, const char* title) {
    canvas_draw_box(c, 0, 0, 128, 13);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontPrimary);
    draw_str_center(c, 64, 10, title);
    canvas_set_color(c, ColorBlack);
}

static void learn_draw_key(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_circle(c, x, y, 4);
    canvas_draw_disc(c, x, y, 1);
    canvas_draw_line(c, x + 4, y, x + 15, y);
    canvas_draw_line(c, x + 11, y, x + 11, y + 4);
    canvas_draw_line(c, x + 15, y, x + 15, y + 3);
}

/* Listening rings, expanding away from the key. Purely cosmetic, and the only
 * thing on screen telling the operator the app has not simply frozen while it
 * waits for a button press that may be a minute away. */
static void learn_draw_rings(Canvas* c, int32_t x, int32_t y, uint8_t anim) {
    for(uint8_t i = 0; i < 3; i++) {
        int32_t r = 8 + ((anim + i * 4) % 12);
        canvas_draw_circle(c, x, y, (size_t)r);
    }
}

/* The captured envelope, drawn as a filled area across the box. The eight bins
 * are interpolated so it reads as a waveform rather than as a bar chart of a
 * thing that is not really eight values wide. */
static void learn_draw_envelope(Canvas* c, const uint8_t* env, int32_t x0, int32_t y0, int32_t w, int32_t h) {
    canvas_draw_frame(c, x0, y0, (size_t)w, (size_t)h);

    int32_t inner_w = w - 2;
    int32_t inner_h = h - 2;
    int32_t base = y0 + h - 1;

    for(int32_t i = 0; i < inner_w; i++) {
        /* position along the envelope, in eighths scaled by 256 */
        int32_t pos = (i * (CDR_ENV_BINS - 1) * 256) / (inner_w - 1);
        int32_t bin = pos / 256;
        int32_t frac = pos % 256;
        if(bin >= CDR_ENV_BINS - 1) {
            bin = CDR_ENV_BINS - 2;
            frac = 256;
        }
        int32_t v = ((int32_t)env[bin] * (256 - frac) + (int32_t)env[bin + 1] * frac) / 256;
        int32_t bar = (v * inner_h) / 255;
        if(bar < 1) bar = 1;
        canvas_draw_line(c, x0 + 1 + i, base - 1, x0 + 1 + i, base - bar);
    }
}

static void learn_view_draw(Canvas* canvas, void* model) {
    LearnModel* m = model;
    const LearnState* st = &m->st;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(!st->radio_ok) {
        canvas_set_font(canvas, FontPrimary);
        draw_str_center(canvas, 64, 30, "No CC1101");
        return;
    }

    if(st->got < st->need) {
        learn_draw_title(canvas, "LEARN YOUR KEY");

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 24, "Hold the key against the");
        canvas_draw_str(canvas, 2, 33, "Flipper and press LOCK.");

        snprintf(
            buf,
            sizeof(buf),
            "%u of %u captured",
            (unsigned)st->got,
            (unsigned)st->need);
        canvas_draw_str(canvas, 2, 47, buf);

        /* Two presses, not one: a single reading can be a reflection off the
         * car body, and two agreeing readings cannot. */
        canvas_draw_str(canvas, 2, 63, st->got ? "Press it once more." : "Waiting for a burst...");

        learn_draw_rings(canvas, 100, 40, m->anim);
        learn_draw_key(canvas, 93, 40);
        return;
    }

    learn_draw_title(canvas, "KEY CAPTURED");

    learn_draw_envelope(canvas, st->fob.env, 2, 16, 124, 24);

    canvas_set_font(canvas, FontSecondary);
    uint8_t band = st->fob.band < CdrBandCount ? st->fob.band : CdrBand433;
    unsigned dur = st->fob.dur_ms > 999u ? 999u : st->fob.dur_ms;
    int peak = st->fob.peak_dbm;
    if(peak < -199) peak = -199;
    if(peak > 0) peak = 0;
    snprintf(buf, sizeof(buf), "%s  %u ms  %d dBm", cdr_bands[band].label, dur, peak);
    canvas_draw_str(canvas, 2, 50, buf);

    canvas_draw_line(canvas, 0, 54, 127, 54);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(canvas, 2, 63, "OK save");
    canvas_draw_str(canvas, 74, 63, "Up retry");
}

static bool learn_view_input(InputEvent* event, void* context) {
    LearnView* v = context;
    if(event->type != InputTypeShort) return false;

    bool ready = false;
    with_view_model(
        v->view, LearnModel * m, { ready = m->st.got >= m->st.need; }, false);
    if(!ready) return false;

    if(event->key == InputKeyOk) {
        if(v->cb) v->cb(v->context, LearnEventSave);
        return true;
    }
    if(event->key == InputKeyUp) {
        if(v->cb) v->cb(v->context, LearnEventRetry);
        return true;
    }
    return false;
}

LearnView* learn_view_alloc(void) {
    LearnView* v = malloc(sizeof(LearnView));
    memset(v, 0, sizeof(LearnView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(LearnModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, learn_view_draw);
    view_set_input_callback(v->view, learn_view_input);

    with_view_model(
        v->view,
        LearnModel * m,
        {
            memset(m, 0, sizeof(LearnModel));
            m->st.need = 2;
            m->st.radio_ok = true;
        },
        false);

    return v;
}

void learn_view_free(LearnView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* learn_view_get_view(LearnView* v) {
    furi_assert(v);
    return v->view;
}

void learn_view_set_callback(LearnView* v, LearnCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->context = context;
}

void learn_view_set_state(LearnView* v, const LearnState* state) {
    furi_assert(v);
    furi_assert(state);
    with_view_model(
        v->view, LearnModel * m, { m->st = *state; }, true);
}

void learn_view_tick(LearnView* v) {
    furi_assert(v);
    with_view_model(
        v->view, LearnModel * m, { m->anim = (uint8_t)((m->anim + 1) % 12); }, true);
}
