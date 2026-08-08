#include "splash_view.h"

#include <furi.h>
#include <string.h>

/* Phase boundaries, in percent of the intro. */
#define SP_SWING_END 58
#define SP_WORD_AT 66
#define SP_TAG_AT 80

/* The doorframe. */
#define SP_FX 49 /* frame left, which is also the hinge line */
#define SP_FY 2
#define SP_FW 30 /* taller than it is wide, or it reads as a picture frame */
#define SP_FH 44

struct SplashView {
    View* view;
    SplashSkipCallback cb;
    void* context;
};

typedef struct {
    uint8_t progress;
} SplashModel;

static void draw_str_center(Canvas* c, int32_t cx, int32_t y, const char* s) {
    canvas_draw_str(c, cx - (int32_t)canvas_string_width(c, s) / 2, y, s);
}

/* Three knuckles on the jamb. The hinge is the whole point of the name, so it
 * gets drawn even at this size. */
static void splash_draw_hinge(Canvas* c, bool sealed) {
    for(int32_t i = 0; i < 3; i++) {
        int32_t y = SP_FY + 7 + i * 15;
        canvas_draw_box(c, SP_FX - 3, y, 4, 7);
    }
    if(sealed) {
        /* A little glint, once the door is shut. */
        canvas_draw_line(c, SP_FX - 8, SP_FY + 10, SP_FX - 5, SP_FY + 10);
        canvas_draw_line(c, SP_FX - 8, SP_FY + 40, SP_FX - 5, SP_FY + 40);
    }
}

/* @p shut runs 0 (wide open, seen edge-on and flared by perspective) to 100
 * (flush with the frame). */
static void splash_draw_door(Canvas* c, uint8_t shut) {
    int32_t pw = 8 + ((int32_t)(SP_FW - 12) * shut) / 100;
    int32_t flare = 9 - (9 * (int32_t)shut) / 100;

    int32_t hx = SP_FX + 2;
    int32_t ty = SP_FY + 2;
    int32_t by = SP_FY + SP_FH - 2;
    int32_t fx = hx + pw;

    canvas_draw_line(c, hx, ty, fx, ty - flare);
    canvas_draw_line(c, hx, by, fx, by + flare);
    canvas_draw_line(c, hx, ty, hx, by);
    canvas_draw_line(c, fx, ty - flare, fx, by + flare);

    /* Panel inset and handle, so it reads as a door and not a wedge. */
    if(pw > 16) {
        canvas_draw_line(c, hx + 4, ty + 5, fx - 4, ty - flare + 5);
        canvas_draw_line(c, hx + 4, by - 5, fx - 4, by + flare - 5);
        canvas_draw_line(c, hx + 4, ty + 5, hx + 4, by - 5);
        canvas_draw_line(c, fx - 4, ty - flare + 5, fx - 4, by + flare - 5);
    }
    /* Handle. A short bar rather than a dot -- one pixel of dot is noise, and
     * the handle is what tells you which edge is free. */
    canvas_draw_line(c, fx - 3, (ty + by) / 2 - 2, fx - 3, (ty + by) / 2 + 2);
}

/* The thing kept outside. It drifts in from the left and stops at the jamb. */
static void splash_draw_key(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_circle(c, x, y, 3);
    canvas_draw_line(c, x + 3, y, x + 12, y);
    canvas_draw_line(c, x + 9, y, x + 9, y + 3);
    canvas_draw_line(c, x + 12, y, x + 12, y + 2);
}

static void splash_view_draw(Canvas* canvas, void* model) {
    SplashModel* m = model;
    uint8_t p = m->progress;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    uint8_t shut = p >= SP_SWING_END ? 100 : (uint8_t)(((uint32_t)p * 100u) / SP_SWING_END);
    bool sealed = p >= SP_SWING_END;

    /* Frame. It thickens the moment the door seats. */
    canvas_draw_frame(canvas, SP_FX, SP_FY, SP_FW, SP_FH);
    if(sealed) canvas_draw_frame(canvas, SP_FX - 1, SP_FY - 1, SP_FW + 2, SP_FH + 2);

    splash_draw_hinge(canvas, sealed);
    splash_draw_door(canvas, shut);

    /* The key approaches while the door is still moving and gets no further. */
    int32_t kx = 8 + (22 * (int32_t)(shut > 100 ? 100 : shut)) / 100;
    if(kx > 28) kx = 28;
    splash_draw_key(canvas, kx, SP_FY + SP_FH / 2);

    if(p >= SP_WORD_AT) {
        canvas_set_font(canvas, FontPrimary);
        draw_str_center(canvas, 64, 54, "CARDEA");
    }
    if(p >= SP_TAG_AT) {
        canvas_set_font(canvas, FontSecondary);
        draw_str_center(canvas, 64, 63, "relay attack watch");
    }
}

static bool splash_view_input(InputEvent* event, void* context) {
    SplashView* v = context;
    if(event->type == InputTypeShort || event->type == InputTypeLong) {
        if(v->cb) v->cb(v->context);
        return true;
    }
    return true;
}

SplashView* splash_view_alloc(void) {
    SplashView* v = malloc(sizeof(SplashView));
    memset(v, 0, sizeof(SplashView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(SplashModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, splash_view_draw);
    view_set_input_callback(v->view, splash_view_input);

    return v;
}

void splash_view_free(SplashView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* splash_view_get_view(SplashView* v) {
    furi_assert(v);
    return v->view;
}

void splash_view_set_skip_callback(SplashView* v, SplashSkipCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->context = context;
}

void splash_view_set_progress(SplashView* v, uint8_t progress) {
    furi_assert(v);
    with_view_model(
        v->view, SplashModel * m, { m->progress = progress > 100 ? 100 : progress; }, true);
}
