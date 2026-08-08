#include "primer_view.h"

#include <furi.h>
#include "../helpers/cdr_detect.h"

#include <stdio.h>
#include <string.h>

/* Ticks a panel holds before turning itself. Long enough to read two lines of
 * caption without hurrying, short enough that nobody has to press anything. */
#define PRIMER_DWELL 45

/* --- layout --- */
#define P_TITLE_BASE 9
#define P_RULE1_Y 11
#define P_ART_TOP 13
#define P_ART_BOT 45
#define P_RULE2_Y 47
#define P_CAP1 55
#define P_CAP2 63
#define P_GROUND 41

struct PrimerView {
    View* view;
};

typedef struct {
    uint8_t panel;
    uint8_t phase; /* animation frame, wraps at 60 */
    uint8_t dwell;
} PrimerModel;

/* ------------------------------------------------------------------ *
 * Integer trigonometry
 *
 * Angles are counted in sixths of a right angle -- one unit is six degrees --
 * because that is the resolution an arc drawn across twenty pixels can
 * actually show, and it fits in a sixteen-entry table.
 * ------------------------------------------------------------------ */

static const int16_t sin256[16] =
    {0, 27, 53, 79, 104, 128, 150, 171, 190, 207, 222, 234, 244, 251, 255, 256};

static int32_t isin6(int32_t i) {
    i = ((i % 60) + 60) % 60;
    if(i <= 15) return sin256[i];
    if(i <= 30) return sin256[30 - i];
    if(i <= 45) return -sin256[i - 30];
    return -sin256[60 - i];
}

static int32_t icos6(int32_t i) {
    return isin6(i + 15);
}

static void draw_arc(Canvas* c, int32_t cx, int32_t cy, int32_t r, int32_t i0, int32_t i1) {
    if(r <= 0) return;
    int32_t px = cx + (r * icos6(i0)) / 256;
    int32_t py = cy - (r * isin6(i0)) / 256;
    for(int32_t i = i0 + 1; i <= i1; i++) {
        int32_t x = cx + (r * icos6(i)) / 256;
        int32_t y = cy - (r * isin6(i)) / 256;
        canvas_draw_line(c, px, py, x, y);
        px = x;
        py = y;
    }
}

/* Three arcs marching outward from a source. @p dir is +1 for rightward, -1
 * for leftward. */
static void draw_waves(
    Canvas* c,
    int32_t cx,
    int32_t cy,
    int32_t r0,
    int32_t step,
    int32_t phase,
    int32_t dir,
    int32_t spread) {
    for(int32_t k = 0; k < 3; k++) {
        int32_t r = r0 + ((phase + k * step) % (step * 3));
        if(dir > 0) {
            draw_arc(c, cx, cy, r, -spread, spread);
        } else {
            draw_arc(c, cx, cy, r, 30 - spread, 30 + spread);
        }
    }
}

static void draw_dashed(Canvas* c, int32_t x0, int32_t x1, int32_t y) {
    for(int32_t x = x0; x < x1; x += 4) canvas_draw_line(c, x, y, x + 1, y);
}

/* ------------------------------------------------------------------ *
 * Cast
 * ------------------------------------------------------------------ */

static void draw_car(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_rframe(c, x, y - 7, 27, 7, 2);
    canvas_draw_line(c, x + 6, y - 7, x + 9, y - 12);
    canvas_draw_line(c, x + 9, y - 12, x + 18, y - 12);
    canvas_draw_line(c, x + 18, y - 12, x + 21, y - 7);
    canvas_draw_line(c, x + 14, y - 12, x + 14, y - 7);
    canvas_draw_disc(c, x + 7, y, 2);
    canvas_draw_disc(c, x + 20, y, 2);
}

static void draw_house(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_frame(c, x, y - 12, 21, 12);
    canvas_draw_line(c, x - 2, y - 12, x + 10, y - 19);
    canvas_draw_line(c, x + 10, y - 19, x + 22, y - 12);
    canvas_draw_frame(c, x + 8, y - 6, 5, 6);
}

static void draw_person(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_circle(c, x, y - 11, 2);
    canvas_draw_line(c, x, y - 9, x, y - 4);
    canvas_draw_line(c, x - 3, y - 7, x + 3, y - 7);
    canvas_draw_line(c, x, y - 4, x - 3, y);
    canvas_draw_line(c, x, y - 4, x + 3, y);
}

static void draw_key(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_circle(c, x, y, 3);
    canvas_draw_line(c, x + 3, y, x + 11, y);
    canvas_draw_line(c, x + 8, y, x + 8, y + 3);
    canvas_draw_line(c, x + 11, y, x + 11, y + 2);
}

static void draw_flipper(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_rframe(c, x, y, 24, 15, 2);
    canvas_draw_frame(c, x + 2, y + 2, 14, 8);
    canvas_draw_disc(c, x + 20, y + 6, 2);
    canvas_draw_line(c, x + 3, y + 12, x + 12, y + 12);
}

/* A 125 kHz loop antenna: concentric turns, seen edge on. */
static void draw_coil(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_circle(c, x, y, 9);
    canvas_draw_circle(c, x, y, 7);
    canvas_draw_circle(c, x, y, 5);
}

static void draw_cross(Canvas* c, int32_t x, int32_t y, int32_t r) {
    canvas_draw_line(c, x - r, y - r, x + r, y + r);
    canvas_draw_line(c, x - r, y + r, x + r, y - r);
    canvas_draw_line(c, x - r, y - r + 1, x + r - 1, y + r);
    canvas_draw_line(c, x - r, y + r - 1, x + r - 1, y - r);
}

/* ------------------------------------------------------------------ *
 * Panels
 * ------------------------------------------------------------------ */

typedef struct {
    const char* title;
    const char* cap1;
    const char* cap2;
} PanelText;

/* Titles are kept short so the page tag on the right can never be reached, and
 * captions to about 25 characters so they cannot run off the edge. Both limits
 * came from looking at the mockups, where both were being broken. */
static const PanelText panel_text[PRIMER_PANELS] = {
    {"Ask and answer", "The car asks in a whisper.", "The key shouts its answer."},
    {"Two boxes", "One box at the car, one", "at your door. Ask relayed."},
    {"Answer travels", "Nobody relays the answer.", "It reaches the car alone."},
    {"What it hears", "So it waits in the car for", "a key with nobody there."},
    {"The deaf half", "125 kHz is magnetic. It", "dies within a metre."},
    {"Two of three", "One burst never counts.", "Two families must agree."},
};

static void primer_panel_0(Canvas* c, int32_t ph) {
    draw_car(c, 4, P_GROUND);
    draw_key(c, 104, 26);

    /* The car's question: short range, and drawn short. */
    draw_waves(c, 32, P_GROUND - 9, 4, 4, ph % 12, +1, 6);
    /* The key's answer: long range, and drawn long. */
    draw_waves(c, 100, 26, 8, 8, ph % 24, -1, 8);
}

static void primer_panel_1(Canvas* c, int32_t ph) {
    draw_car(c, 2, P_GROUND);
    draw_house(c, 100, P_GROUND);
    draw_person(c, 40, P_GROUND);
    draw_person(c, 74, P_GROUND);

    draw_waves(c, 30, P_GROUND - 9, 3, 3, ph % 9, +1, 5);
    draw_waves(c, 88, P_GROUND - 12, 3, 3, ph % 9, +1, 5);

    /* The relayed whisper, crossing the gap between the two of them. */
    draw_dashed(c, 44, 71, 20);
    int32_t dot = 44 + ((ph * 2) % 27);
    canvas_draw_disc(c, dot, 20, 1);
}

static void primer_panel_2(Canvas* c, int32_t ph) {
    draw_car(c, 2, P_GROUND);
    draw_house(c, 100, P_GROUND);
    draw_key(c, 104, P_GROUND - 6);

    /* The link they had to build, still there, still not carrying this. */
    draw_dashed(c, 34, 96, P_GROUND - 4);

    /* The reply, travelling on its own. A wave rather than arcs, because the
     * point of this panel is the distance it covers. */
    int32_t prev_y = 0;
    for(int32_t x = 32; x <= 98; x++) {
        int32_t y = 22 + (6 * isin6((x - 32) * 3 - ph * 3)) / 256;
        if(x > 32) canvas_draw_line(c, x - 1, prev_y, x, y);
        prev_y = y;
    }
    canvas_draw_line(c, 32, 22, 36, 19);
    canvas_draw_line(c, 32, 22, 36, 25);
}

static void primer_panel_3(Canvas* c, int32_t ph) {
    draw_car(c, 2, P_GROUND);
    draw_flipper(c, 74, 20);

    /* Arriving, not leaving: the arcs march inward toward the receiver. */
    for(int32_t k = 0; k < 3; k++) {
        int32_t r = 26 - ((ph + k * 6) % 18);
        if(r > 6) draw_arc(c, 74, 27, r, 24, 36);
    }
    canvas_set_font(c, FontKeyboard);
    canvas_draw_str(c, 76, 44, "listening");
}

static void primer_panel_4(Canvas* c, int32_t ph) {
    /* The half it is deaf to. */
    draw_coil(c, 24, 27);
    draw_cross(c, 24, 27, 12);

    /* The half it hears. */
    draw_key(c, 84, 27);
    draw_waves(c, 82, 27, 6, 6, ph % 18, -1, 8);

    canvas_set_font(c, FontKeyboard);
    canvas_draw_str(c, 8, 44, "125 kHz");
    canvas_draw_str(c, 78, 44, "433 MHz");
}

static void primer_panel_5(Canvas* c, int32_t ph) {
    static const char* const names[3] = {"UNSOL", "RHYTHM", "CLONE"};
    static const uint8_t caps[3] = {CDR_W_UNSOLICITED, CDR_W_CADENCE, CDR_W_CLONE};

    for(uint8_t i = 0; i < 3; i++) {
        int32_t x = 2 + i * 42;
        canvas_draw_rframe(c, x, 15, 40, 13, 2);

        /* Each chip fills in turn, so the picture says "these are separate
         * measurements" without a word of caption. */
        int32_t slot = (ph / 6) % 4;
        uint8_t score = slot > i ? caps[i] : 0;
        if(score) {
            int32_t w = ((int32_t)score * 38) / caps[i];
            canvas_draw_rbox(c, x + 1, 16, (size_t)w, 11, 1);
        }
        canvas_set_font(c, FontKeyboard);
        canvas_set_color(c, ColorXOR);
        int32_t tw = (int32_t)canvas_string_width(c, names[i]);
        canvas_draw_str(c, x + (40 - tw) / 2, 24, names[i]);
        canvas_set_color(c, ColorBlack);
    }

    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 2, 40, "...one must be RHYTHM.");
}

/* ------------------------------------------------------------------ */

static void primer_view_draw(Canvas* canvas, void* model) {
    PrimerModel* m = model;
    uint8_t panel = m->panel < PRIMER_PANELS ? m->panel : 0;
    char tag[8];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, P_TITLE_BASE, panel_text[panel].title);

    canvas_set_font(canvas, FontKeyboard);
    snprintf(tag, sizeof(tag), "%u/%u", (unsigned)(panel + 1), (unsigned)PRIMER_PANELS);
    canvas_draw_str(canvas, 126 - (int32_t)canvas_string_width(canvas, tag), P_TITLE_BASE, tag);

    canvas_draw_line(canvas, 0, P_RULE1_Y, 127, P_RULE1_Y);
    canvas_draw_line(canvas, 0, P_RULE2_Y, 127, P_RULE2_Y);

    switch(panel) {
    case 0:
        primer_panel_0(canvas, m->phase);
        break;
    case 1:
        primer_panel_1(canvas, m->phase);
        break;
    case 2:
        primer_panel_2(canvas, m->phase);
        break;
    case 3:
        primer_panel_3(canvas, m->phase);
        break;
    case 4:
        primer_panel_4(canvas, m->phase);
        break;
    default:
        primer_panel_5(canvas, m->phase);
        break;
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, P_CAP1, panel_text[panel].cap1);
    canvas_draw_str(canvas, 2, P_CAP2, panel_text[panel].cap2);
}

static bool primer_view_input(InputEvent* event, void* context) {
    PrimerView* v = context;
    if(event->type != InputTypeShort) return false;
    if(event->key != InputKeyLeft && event->key != InputKeyRight) return false;

    with_view_model(
        v->view,
        PrimerModel * m,
        {
            if(event->key == InputKeyRight) {
                m->panel = (uint8_t)((m->panel + 1) % PRIMER_PANELS);
            } else {
                m->panel = (uint8_t)((m->panel + PRIMER_PANELS - 1) % PRIMER_PANELS);
            }
            /* Taking manual control resets the clock, so the panel you just
             * asked for does not turn itself a moment later. */
            m->dwell = 0;
        },
        true);
    return true;
}

PrimerView* primer_view_alloc(void) {
    PrimerView* v = malloc(sizeof(PrimerView));
    memset(v, 0, sizeof(PrimerView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(PrimerModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, primer_view_draw);
    view_set_input_callback(v->view, primer_view_input);

    return v;
}

void primer_view_free(PrimerView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* primer_view_get_view(PrimerView* v) {
    furi_assert(v);
    return v->view;
}

void primer_view_tick(PrimerView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        PrimerModel * m,
        {
            m->phase = (uint8_t)((m->phase + 1) % 60);
            if(++m->dwell >= PRIMER_DWELL) {
                m->dwell = 0;
                m->panel = (uint8_t)((m->panel + 1) % PRIMER_PANELS);
            }
        },
        true);
}

void primer_view_reset(PrimerView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        PrimerModel * m,
        {
            m->panel = 0;
            m->phase = 0;
            m->dwell = 0;
        },
        true);
}
