#include "guard_view.h"

#include <furi.h>
#include <gui/elements.h>

#include <stdio.h>
#include <string.h>

#define ALERT_PAGES 3

struct GuardView {
    View* view;
    GuardCallback cb;
    void* context;
};

typedef struct {
    GuardState st;
    uint8_t page;
    uint8_t alert_page;
    uint8_t blink;
} GuardModel;

/* ------------------------------------------------------------------ *
 * Small drawing helpers
 * ------------------------------------------------------------------ */

static void draw_str_right(Canvas* c, int32_t x_right, int32_t y, const char* s) {
    canvas_draw_str(c, x_right - (int32_t)canvas_string_width(c, s), y, s);
}

static void draw_str_center(Canvas* c, int32_t cx, int32_t y, const char* s) {
    canvas_draw_str(c, cx - (int32_t)canvas_string_width(c, s) / 2, y, s);
}

/* Speaker with a slash. Drawn rather than iconned because at eight pixels an
 * icon file is more trouble than the six lines it replaces. */
static void draw_mute_glyph(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_box(c, x, y + 2, 2, 3);
    for(int32_t i = 0; i < 4; i++) {
        canvas_draw_line(c, x + 2 + i, y + 3 - i, x + 2 + i, y + 3 + i);
    }
    canvas_draw_line(c, x - 1, y + 7, x + 8, y - 1);
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
    if(h) {
        snprintf(out, len, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    } else {
        snprintf(out, len, "%02u:%02u", (unsigned)m, (unsigned)s);
    }
}

/* ------------------------------------------------------------------ *
 * Header - shared by all three pages
 * ------------------------------------------------------------------ */

static void guard_draw_header(Canvas* canvas, const GuardModel* m) {
    const GuardState* st = &m->st;
    uint8_t band = st->snap.band < CdrBandCount ? st->snap.band : CdrBand433;
    char buf[16];

    /* Filled while camped on a band, hollow while hopping. The distinction
     * matters: camped means every frame on that band is being seen, hopping
     * means roughly a fifth of them are. */
    if(st->snap.camped) {
        canvas_draw_box(canvas, 0, 2, 5, 5);
    } else {
        canvas_draw_frame(canvas, 0, 2, 5, 5);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 7, G_HDR_BASE, cdr_bands[band].label);

    /* The away/here pill. This is the single most important control in the
     * app -- it is the difference between "a key answered" and "a key answered
     * and nobody was there to ask". */
    canvas_set_font(canvas, FontKeyboard);
    const char* pill;
    if(st->arm_in_s) {
        uint16_t s = st->arm_in_s > 999 ? 999 : st->arm_in_s;
        snprintf(buf, sizeof(buf), "%us", (unsigned)s);
        pill = buf;
    } else {
        pill = st->armed ? "AWAY" : "HERE";
    }

    if(st->armed) {
        canvas_draw_rbox(canvas, G_PILL_X, 0, G_PILL_W, 9, 2);
        canvas_set_color(canvas, ColorWhite);
        draw_str_center(canvas, G_PILL_X + G_PILL_W / 2, G_HDR_BASE, pill);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_rframe(canvas, G_PILL_X, 0, G_PILL_W, 9, 2);
        draw_str_center(canvas, G_PILL_X + G_PILL_W / 2, G_HDR_BASE, pill);
    }

    if(st->muted) draw_mute_glyph(canvas, G_MUTE_X, 0);

    for(uint8_t i = 0; i < G_PAGES; i++) {
        int32_t x = G_DOTS_X + i * 4;
        if(i == m->page) {
            canvas_draw_box(canvas, x, 4, 3, 3);
        } else {
            canvas_draw_dot(canvas, x + 1, 5);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    format_elapsed(buf, sizeof(buf), st->snap.elapsed_ms);
    draw_str_right(canvas, 127, G_HDR_BASE, buf);

    canvas_draw_line(canvas, 0, G_RULE_Y, 127, G_RULE_Y);
}

/* ------------------------------------------------------------------ *
 * Page 0 - the watch
 * ------------------------------------------------------------------ */

static void guard_draw_tag(Canvas* canvas, int32_t x, const char* text) {
    canvas_set_font(canvas, FontKeyboard);
    int32_t w = (int32_t)canvas_string_width(canvas, text) + 6;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x - 1, G_RASTER_TOP - 1, (size_t)w + 2, 10);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, x, G_RASTER_TOP, (size_t)w, 9, 2);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, x + 3, G_RASTER_TOP + 7, text);
    canvas_set_color(canvas, ColorBlack);
}

static void guard_draw_raster(Canvas* canvas, const GuardModel* m) {
    const CdrSnapshot* s = &m->st.snap;

    /* The line a burst has to clear before it is allowed to be evidence. Every
     * bar under it was seen and deliberately not counted, which is worth being
     * able to see. */
    int32_t th = ((int32_t)cdr_sens_signal_db(m->st.sens) * G_RASTER_H) / CDR_RASTER_MAX_DB;
    int32_t ty = G_RASTER_BASE - th;
    if(ty < G_RASTER_TOP) ty = G_RASTER_TOP;
    for(int32_t x = G_RASTER_X0; x < 126; x += 3) canvas_draw_dot(canvas, x, ty);

    for(uint16_t i = 0; i < CDR_RASTER_LEN; i++) {
        uint8_t idx = (uint8_t)((s->raster_head + 1u + i) % CDR_RASTER_LEN);
        uint8_t v = s->raster[idx];
        if(!v) continue;

        int32_t h = ((int32_t)v * G_RASTER_H) / CDR_RASTER_MAX_DB;
        if(h < 1) h = 1;
        if(h > G_RASTER_H) h = G_RASTER_H;
        int32_t x = G_RASTER_X0 + (int32_t)i;
        canvas_draw_line(canvas, x, G_RASTER_BASE, x, G_RASTER_BASE - h + 1);
    }

    canvas_draw_line(canvas, 0, G_RULE2_Y, 127, G_RULE2_Y);

    if(m->st.snap.verdict.beacon) guard_draw_tag(canvas, G_RASTER_X0, "BEACON");
    if(m->st.snap.link.active) guard_draw_tag(canvas, 93, "HELD");
}

static void
    guard_draw_chip(Canvas* canvas, int32_t x, const char* label, uint8_t score, uint8_t ceiling) {
    canvas_draw_rframe(canvas, x, G_CHIP_Y, G_CHIP_W, G_CHIP_H, 2);

    /* The fill covers the full interior height, so a glyph is either wholly
     * over ink or wholly over paper and never sliced in half by the edge of
     * the fill. */
    if(score && ceiling) {
        int32_t fill = ((int32_t)score * (G_CHIP_W - 2)) / ceiling;
        if(fill < 3) fill = 3;
        canvas_draw_rbox(canvas, x + 1, G_CHIP_Y + 1, (size_t)fill, G_CHIP_H - 2, 1);
    }

    /* XOR so the label stays readable whether the chip behind it is empty,
     * half full or brimming. */
    canvas_set_font(canvas, FontKeyboard);
    canvas_set_color(canvas, ColorXOR);
    draw_str_center(canvas, x + G_CHIP_W / 2, G_CHIP_BASE, label);
    canvas_set_color(canvas, ColorBlack);
}

static void guard_draw_verdict(Canvas* canvas, const GuardModel* m) {
    const CdrVerdict* v = &m->st.snap.verdict;
    char buf[8];

    guard_draw_chip(canvas, 2, "UNSOL", v->s_unsolicited, CDR_W_UNSOLICITED);
    guard_draw_chip(canvas, 44, "RHYTHM", v->s_cadence, CDR_W_CADENCE);
    guard_draw_chip(canvas, 86, "CLONE", v->s_clone, CDR_W_CLONE);

    /* Where the verdict changes name, marked on the scale so the number has
     * somewhere to be relative to. */
    static const uint8_t ticks[3] = {CDR_T_ODD, CDR_T_SUSPICIOUS, CDR_T_LIKELY};
    for(uint8_t i = 0; i < 3; i++) {
        int32_t x = G_BAR_X + 1 + ((int32_t)ticks[i] * (G_BAR_W - 2)) / 100;
        canvas_draw_line(canvas, x, G_TICK_Y, x, G_TICK_Y + 1);
    }

    canvas_draw_frame(canvas, G_BAR_X, G_BAR_Y, G_BAR_W, G_BAR_H);
    if(v->score) {
        int32_t fill = ((int32_t)v->score * (G_BAR_W - 2)) / 100;
        if(fill < 1) fill = 1;
        canvas_draw_box(canvas, G_BAR_X + 1, G_BAR_Y + 1, (size_t)fill, G_BAR_H - 2);
    }

    uint8_t level = v->level < CdrLevelCount ? v->level : 0;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, G_VERDICT_BASE, cdr_level_labels[level]);

    snprintf(buf, sizeof(buf), "%u", (unsigned)v->score);
    draw_str_right(canvas, 127, G_VERDICT_BASE, buf);
}

/* ------------------------------------------------------------------ *
 * Page 1 - the numbers
 * ------------------------------------------------------------------ */

static void guard_draw_row(Canvas* canvas, int32_t y, const char* label, const char* value) {
    canvas_draw_str(canvas, 2, y, label);
    draw_str_right(canvas, 126, y, value);
}

static void guard_draw_detail(Canvas* canvas, const GuardModel* m) {
    const CdrSnapshot* s = &m->st.snap;
    const CdrVerdict* v = &s->verdict;
    char val[32];

    canvas_set_font(canvas, FontSecondary);
    uint8_t band = s->band < CdrBandCount ? s->band : CdrBand433;

    int16_t floor = s->floor[band];
    if(floor == CDR_DBM_INVALID) {
        snprintf(val, sizeof(val), "--");
    } else {
        int fl = floor < -199 ? -199 : (floor > 0 ? 0 : floor);
        snprintf(val, sizeof(val), "%d dBm", fl);
    }
    guard_draw_row(canvas, 18, "Noise floor", val);

    uint32_t bursts = s->bursts > 99999u ? 99999u : s->bursts;
    snprintf(val, sizeof(val), "%u / %u", (unsigned)bursts, (unsigned)v->total);
    guard_draw_row(canvas, 27, "Bursts / win", val);

    if(s->have_last) {
        int mgn = (int)(s->last.peak_dbm - s->last.floor_dbm);
        if(mgn < 0) mgn = 0;
        if(mgn > 99) mgn = 99;
        unsigned dur = s->last.dur_ms > 999u ? 999u : s->last.dur_ms;
        snprintf(val, sizeof(val), "%ums +%udB", dur, (unsigned)mgn);
    } else {
        snprintf(val, sizeof(val), "none yet");
    }
    guard_draw_row(canvas, 36, "Last burst", val);

    if(v->period_ms && v->jitter_pct != CDR_JITTER_NA) {
        unsigned j = v->jitter_pct > 999u ? 999u : v->jitter_pct;
        snprintf(val, sizeof(val), "%ums +-%u%%", (unsigned)v->period_ms, j);
    } else {
        snprintf(val, sizeof(val), "--");
    }
    guard_draw_row(canvas, 45, "Poll period", val);

    if(v->clone_pct == CDR_PCT_NA) {
        snprintf(val, sizeof(val), "--");
    } else {
        snprintf(val, sizeof(val), "%u%%", (unsigned)v->clone_pct);
    }
    guard_draw_row(canvas, 54, "Clone rate", val);

    unsigned hz = s->sample_hz > 9999u ? 9999u : s->sample_hz;
    snprintf(
        val, sizeof(val), "%s / %uHz", s->link.active ? "held" : "no", hz);
    guard_draw_row(canvas, 63, "Band held", val);
}

/* ------------------------------------------------------------------ *
 * Page 2 - the bands
 * ------------------------------------------------------------------ */

static void guard_draw_bands(Canvas* canvas, const GuardModel* m) {
    const CdrSnapshot* s = &m->st.snap;
    char buf[16];

    canvas_set_font(canvas, FontKeyboard);

    for(uint8_t i = 0; i < CdrBandCount; i++) {
        int32_t y = 19 + i * 9;
        bool on = (m->st.band_mask & (uint8_t)(1u << i)) != 0;
        bool here = s->band == i;

        if(here) canvas_draw_box(canvas, 0, y - 7, 4, 7);
        if(m->st.pinned == i) canvas_draw_frame(canvas, 0, y - 7, 4, 7);

        canvas_draw_str(canvas, 6, y, cdr_bands[i].label);

        if(!on) {
            /* Struck through: watched bands and ignored bands have to be
             * distinguishable at a glance in the dark. */
            canvas_draw_line(canvas, 5, y - 3, 43, y - 3);
            canvas_draw_str(canvas, 52, y, "off");
            continue;
        }

        int16_t floor = s->floor[i];
        if(floor == CDR_DBM_INVALID) {
            snprintf(buf, sizeof(buf), "----");
        } else {
            int fl = floor < -199 ? -199 : (floor > 0 ? 0 : floor);
            snprintf(buf, sizeof(buf), "%d", fl);
        }
        canvas_draw_str(canvas, 46, y, buf);

        canvas_draw_frame(canvas, 72, y - 6, 32, 6);
        if(s->peak[i] != CDR_DBM_INVALID && floor != CDR_DBM_INVALID) {
            int32_t margin = s->peak[i] - floor;
            if(margin < 0) margin = 0;
            if(margin > CDR_RASTER_MAX_DB) margin = CDR_RASTER_MAX_DB;
            int32_t w = (margin * 30) / CDR_RASTER_MAX_DB;
            if(w > 0) canvas_draw_box(canvas, 73, y - 5, (size_t)w, 4);
        }

        unsigned hits = s->hits[i] > 9999u ? 9999u : s->hits[i];
        snprintf(buf, sizeof(buf), "%u", hits);
        draw_str_right(canvas, 127, y, buf);
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_line(canvas, 0, 56, 127, 56);
    canvas_draw_str(canvas, 2, 63, "Up/Dn pins the receiver");
}

/* ------------------------------------------------------------------ *
 * The alert
 * ------------------------------------------------------------------ */

static void guard_draw_alert_banner(Canvas* canvas, const GuardModel* m, const char* title) {
    /* Inverted, and for the first seconds it blinks. A watchdog that fires
     * while the owner is walking away has one job: be seen. */
    bool on = (m->blink & 1u) == 0;
    if(on) {
        canvas_draw_box(canvas, 0, 0, 128, 14);
        canvas_set_color(canvas, ColorWhite);
    }
    /* Left-aligned, not centred: the page counter lives in the top right and
     * a centred "RELAY LIKELY" runs straight underneath it. */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 10, title);
    canvas_set_color(canvas, ColorBlack);
    if(!on) canvas_draw_line(canvas, 0, 13, 127, 13);
}

static void guard_draw_alert(Canvas* canvas, const GuardModel* m) {
    const CdrSnapshot* s = &m->st.snap;
    const CdrVerdict* v = &s->verdict;
    uint8_t band = v->band < CdrBandCount ? v->band : CdrBand433;
    char buf[40];

    if(m->alert_page == 0) {
        guard_draw_alert_banner(canvas, m, "RELAY LIKELY");
        canvas_set_font(canvas, FontSecondary);

        snprintf(
            buf,
            sizeof(buf),
            "%u replies on %s MHz",
            (unsigned)v->matched,
            cdr_bands[band].label);
        canvas_draw_str(canvas, 2, 25, buf);

        if(v->period_ms) {
            unsigned j = v->jitter_pct > 99u ? 99u : v->jitter_pct;
            snprintf(buf, sizeof(buf), "every %u ms, +-%u%%", (unsigned)v->period_ms, j);
        } else {
            snprintf(buf, sizeof(buf), "with no gap to time");
        }
        canvas_draw_str(canvas, 2, 34, buf);

        canvas_draw_str(canvas, 2, 43, "and you were away.");

        canvas_draw_line(canvas, 0, 46, 127, 46);
        canvas_draw_str(canvas, 2, 54, "Your key is answering");
        canvas_draw_str(canvas, 2, 62, "a question nobody asked.");
    } else if(m->alert_page == 1) {
        guard_draw_alert_banner(canvas, m, "WHY IT FIRED");
        canvas_set_font(canvas, FontSecondary);

        static const char* const names[3] = {"Unsolicited", "Machine rhythm", "Identical frames"};
        const uint8_t got[3] = {v->s_unsolicited, v->s_cadence, v->s_clone};
        const uint8_t cap[3] = {CDR_W_UNSOLICITED, CDR_W_CADENCE, CDR_W_CLONE};

        for(uint8_t i = 0; i < 3; i++) {
            int32_t y = 24 + i * 10;
            canvas_draw_str(canvas, 2, y, names[i]);
            canvas_draw_frame(canvas, 88, y - 6, 38, 6);
            if(got[i]) {
                int32_t w = ((int32_t)got[i] * 36) / cap[i];
                if(w < 1) w = 1;
                canvas_draw_box(canvas, 89, y - 5, (size_t)w, 4);
            }
        }

        canvas_draw_line(canvas, 0, 46, 127, 46);
        canvas_draw_str(canvas, 2, 54, "Evidence, not proof --");
        canvas_draw_str(canvas, 2, 62, "the reply is not decoded.");
    } else {
        guard_draw_alert_banner(canvas, m, "WHAT TO DO");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 23, "1 Keys away from doors");
        canvas_draw_str(canvas, 2, 31, "  and windows.");
        canvas_draw_str(canvas, 2, 40, "2 A metal tin will do.");
        canvas_draw_str(canvas, 2, 48, "3 Check the car from");
        canvas_draw_str(canvas, 2, 56, "  indoors. Do not go");
        canvas_draw_str(canvas, 2, 63, "  out to anyone.");
    }

    /* Footer hint, drawn over whatever is behind it so it is always legible. */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 96, 0, 32, 14);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontKeyboard);
    snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(m->alert_page + 1), (unsigned)ALERT_PAGES);
    draw_str_right(canvas, 126, 10, buf);
}

/* ------------------------------------------------------------------ *
 * Draw / input
 * ------------------------------------------------------------------ */

static void guard_view_draw(Canvas* canvas, void* model) {
    GuardModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(!m->st.snap.radio_ok) {
        canvas_set_font(canvas, FontPrimary);
        draw_str_center(canvas, 64, 26, "No CC1101");
        canvas_set_font(canvas, FontSecondary);
        draw_str_center(canvas, 64, 40, "The internal radio did not");
        draw_str_center(canvas, 64, 49, "start. Reboot and retry.");
        return;
    }

    if(m->st.alert) {
        guard_draw_alert(canvas, m);
        return;
    }

    guard_draw_header(canvas, m);

    if(m->page == 0) {
        guard_draw_raster(canvas, m);
        guard_draw_verdict(canvas, m);
    } else if(m->page == 1) {
        guard_draw_detail(canvas, m);
    } else {
        guard_draw_bands(canvas, m);
    }
}

static bool guard_view_input(InputEvent* event, void* context) {
    GuardView* v = context;
    bool consumed = false;

    bool alert = false;
    with_view_model(
        v->view, GuardModel * m, { alert = m->st.alert; }, false);

    if(alert) {
        if(event->type == InputTypeShort || event->type == InputTypeLong) {
            if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                with_view_model(
                    v->view,
                    GuardModel * m,
                    {
                        if(event->key == InputKeyRight) {
                            m->alert_page = (uint8_t)((m->alert_page + 1) % ALERT_PAGES);
                        } else {
                            m->alert_page =
                                (uint8_t)((m->alert_page + ALERT_PAGES - 1) % ALERT_PAGES);
                        }
                    },
                    true);
                return true;
            }
            /* Anything else stands the alert down. Back included: an alarm you
             * cannot silence with the obvious key is an alarm that gets the
             * app closed instead. */
            if(v->cb) v->cb(v->context, GuardEventDismiss);
            return true;
        }
        return event->key == InputKeyBack;
    }

    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyLeft:
            with_view_model(
                v->view,
                GuardModel * m,
                { m->page = (uint8_t)((m->page + G_PAGES - 1) % G_PAGES); },
                true);
            consumed = true;
            break;
        case InputKeyRight:
            with_view_model(
                v->view, GuardModel * m, { m->page = (uint8_t)((m->page + 1) % G_PAGES); }, true);
            consumed = true;
            break;
        case InputKeyUp:
            if(v->cb) v->cb(v->context, GuardEventPinPrev);
            consumed = true;
            break;
        case InputKeyDown:
            if(v->cb) v->cb(v->context, GuardEventPinNext);
            consumed = true;
            break;
        case InputKeyOk:
            if(v->cb) v->cb(v->context, GuardEventArm);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        if(v->cb) v->cb(v->context, GuardEventMute);
        consumed = true;
    }

    return consumed;
}

/* ------------------------------------------------------------------ */

GuardView* guard_view_alloc(void) {
    GuardView* v = malloc(sizeof(GuardView));
    memset(v, 0, sizeof(GuardView));

    v->view = view_alloc();
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(GuardModel));
    view_set_context(v->view, v);
    view_set_draw_callback(v->view, guard_view_draw);
    view_set_input_callback(v->view, guard_view_input);

    with_view_model(
        v->view,
        GuardModel * m,
        {
            memset(m, 0, sizeof(GuardModel));
            m->st.snap.radio_ok = true;
            m->st.snap.verdict.jitter_pct = CDR_JITTER_NA;
            m->st.snap.verdict.clone_pct = CDR_PCT_NA;
            m->st.pinned = CdrBandCount;
            for(uint8_t i = 0; i < CdrBandCount; i++) {
                m->st.snap.floor[i] = CDR_DBM_INVALID;
                m->st.snap.peak[i] = CDR_DBM_INVALID;
            }
        },
        false);

    return v;
}

void guard_view_free(GuardView* v) {
    furi_assert(v);
    view_free(v->view);
    free(v);
}

View* guard_view_get_view(GuardView* v) {
    furi_assert(v);
    return v->view;
}

void guard_view_set_callback(GuardView* v, GuardCallback cb, void* context) {
    furi_assert(v);
    v->cb = cb;
    v->context = context;
}

void guard_view_set_state(GuardView* v, const GuardState* state) {
    furi_assert(v);
    furi_assert(state);
    with_view_model(
        v->view,
        GuardModel * m,
        {
            bool was = m->st.alert;
            m->st = *state;
            if(state->alert && !was) m->alert_page = 0;
        },
        true);
}

void guard_view_tick(GuardView* v) {
    furi_assert(v);
    with_view_model(
        v->view, GuardModel * m, { m->blink++; }, m->st.alert);
}

void guard_view_reset(GuardView* v) {
    furi_assert(v);
    with_view_model(
        v->view,
        GuardModel * m,
        {
            m->page = 0;
            m->alert_page = 0;
            m->blink = 0;
            m->st.alert = false;
        },
        true);
}
