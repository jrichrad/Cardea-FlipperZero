#include "report_view.h"

#include <furi.h>
#include <stdio.h>
#include <string.h>

#define REPORT_PAGES 2

struct ReportView {
    View* view;
};

typedef struct {
    ReportState st;
    uint8_t page;
} ReportModel;

static void draw_str_center(Canvas* c, int32_t cx, int32_t y, const char* s) {
    canvas_draw_str(c, cx - (int32_t)canvas_string_width(c, s) / 2, y, s);
}

static void draw_str_right(Canvas* c, int32_t x_right, int32_t y, const char* s) {
    canvas_draw_str(c, x_right - (int32_t)canvas_string_width(c, s), y, s);
}

static void format_elapsed(char* out, size_t len, uint32_t ms) {
    uint32_t s = ms / 1000u;
    uint32_t h = s / 3600u;
    s -= h * 3600u;
    uint32_t m = s / 60u;
    s -= m * 60u;
    if(h > 99u) h = 99u;
    if(m > 59u) m = 59u;
    if(s > 59u) s = 59u;
    snprintf(out, len, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
}

static void report_draw_title(Canvas* c, const char* title, uint8_t page) {
    char tag[8];
    canvas_draw_box(c, 0, 0, 128, 13);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, title);
    canvas_set_font(c, FontKeyboard);
    snprintf(tag, sizeof(tag), "%u/%u", (unsigned)(page + 1), (unsigned)REPORT_PAGES);
    draw_str_right(c, 126, 10, tag);
    canvas_set_color(c, ColorBlack);
}

static void report_row(Canvas* c, int32_t y, const char* label, const char* value) {
    canvas_draw_str(c, 2, y, label);
    draw_str_right(c, 126, y, value);
}

static void report_draw_summary(Canvas* canvas, const ReportModel* m) {
    const CdrSnapshot* s = &m->st.snap;
    char val[32];

    report_draw_title(canvas, "WATCH REPORT", m->page);
    canvas_set_font(canvas, FontSecondary);

    format_elapsed(val, sizeof(val), s->elapsed_ms);
    report_row(canvas, 22, "Watched for", val);

    uint32_t bursts = s->bursts > 99999u ? 99999u : s->bursts;
    snprintf(val, sizeof(val), "%u", (unsigned)bursts);
    report_row(canvas, 31, "Bursts seen", val);

    uint8_t busiest = 0;
    for(uint8_t i = 1; i < CdrBandCount; i++) {
        if(s->hits[i] > s->hits[busiest]) busiest = i;
    }
    if(s->hits[busiest]) {
        unsigned n = s->hits[busiest] > 9999u ? 9999u : s->hits[busiest];
        snprintf(val, sizeof(val), "%s (%u)", cdr_bands[busiest].label, n);
    } else {
        snprintf(val, sizeof(val), "nothing heard");
    }
    report_row(canvas, 40, "Busiest", val);

    /* Short labels on purpose: "RELAY LIKELY 90" is a wide value, and the
     * mockups showed it walking straight through a longer label. */
    uint8_t level = s->peak_level < CdrLevelCount ? s->peak_level : 0;
    snprintf(val, sizeof(val), "%s %u", cdr_level_labels[level], (unsigned)s->peak_score);
    report_row(canvas, 49, "Worst", val);

    if(s->peak_score) {
        format_elapsed(val, sizeof(val), s->peak_at_ms);
        report_row(canvas, 58, "  ...at", val);
    } else {
        canvas_draw_str(canvas, 2, 58, "  ...nothing to report.");
    }
}

static void report_draw_meaning(Canvas* canvas, const ReportModel* m) {
    const CdrSnapshot* s = &m->st.snap;
    uint8_t level = s->peak_level < CdrLevelCount ? s->peak_level : 0;

    report_draw_title(canvas, "WHAT IT MEANS", m->page);
    canvas_set_font(canvas, FontSecondary);

    /* The plain-language half. A score nobody can interpret is a score nobody
     * acts on. */
    if(level >= CdrLevelLikely) {
        canvas_draw_str(canvas, 2, 23, "A key answered again and");
        canvas_draw_str(canvas, 2, 32, "again, on a machine's");
        canvas_draw_str(canvas, 2, 41, "rhythm, while you were");
        canvas_draw_str(canvas, 2, 50, "away. Treat it as real.");
    } else if(level == CdrLevelSuspicious) {
        canvas_draw_str(canvas, 2, 23, "Unexplained key traffic,");
        canvas_draw_str(canvas, 2, 32, "but without the machine");
        canvas_draw_str(canvas, 2, 41, "rhythm that separates a");
        canvas_draw_str(canvas, 2, 50, "relay from a person.");
    } else if(level == CdrLevelOdd) {
        canvas_draw_str(canvas, 2, 23, "Some traffic worth");
        canvas_draw_str(canvas, 2, 32, "knowing about, nothing");
        canvas_draw_str(canvas, 2, 41, "worth waking you for.");
        canvas_draw_str(canvas, 2, 50, "Most streets look so.");
    } else {
        canvas_draw_str(canvas, 2, 23, "Nothing stood out. That");
        canvas_draw_str(canvas, 2, 32, "is not the same as");
        canvas_draw_str(canvas, 2, 41, "nothing happening -- it");
        canvas_draw_str(canvas, 2, 50, "hears one half, not two.");
    }

    canvas_draw_line(canvas, 0, 54, 127, 54);
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str(
        canvas, 2, 63, m->st.logged ? "logged to events.csv" : "logging is off");
}

static void report_view_draw(Canvas* canvas, void* model) {
    ReportModel* m = model;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(!m->st.valid) {
        canvas_set_font(canvas, FontPrimary);
        draw_str_center(canvas, 64, 26, "No watch yet");
        canvas_set_font(canvas, FontSecondary);
        draw_str_center(canvas, 64, 42, "Run Guard, then come back");
        draw_str_center(canvas, 64, 51, "to see what it heard.");
        return;
    }

    if(m->page == 0) {
        report_draw_summary(canvas, m);
    } else {
        report_draw_meaning(canvas, m);
    }
}

static bool report_view_input(InputEvent* event, void* context) {
    ReportView* v = context;
    if(event->type != InputTypeShort) return false;
    if(event->key != InputKeyLeft && event->key != InputKeyRight) return false;

    with_view_model(
        v->view,
        ReportModel * m,
        {
            if(event->key == InputKeyRight) {
                m->page = (uint8_t)((m->page + 1) % REPORT_PAGES);
            } else {
                m->page = (uint8_t)((m->page + REPORT_PAGES - 1) % REPORT_PAGES);
            }
        },
        true);
    return true;
}

ReportView* report_view_alloc(void) {
    ReportView* v = malloc(sizeof(ReportView));
    memset(v, 0, sizeof(ReportView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(ReportModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, report_view_draw);
    view_set_input_callback(v->view, report_view_input);

    return v;
}

void report_view_free(ReportView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* report_view_get_view(ReportView* v) {
    furi_assert(v);
    return v->view;
}

void report_view_set_state(ReportView* v, const ReportState* state) {
    furi_assert(v);
    furi_assert(state);
    with_view_model(
        v->view,
        ReportModel * m,
        {
            m->st = *state;
            m->page = 0;
        },
        true);
}
