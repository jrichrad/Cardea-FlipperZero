/* The watch screen.
 *
 * Three pages behind Left/Right, plus a full-screen alert that takes the whole
 * display when the verdict reaches RELAY LIKELY. The layout constants are
 * public because tools_gen_mockups.py draws from them: a mockup that invents
 * its own geometry is a picture of an app that does not exist.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/cdr_radio.h"

typedef struct GuardView GuardView;

typedef enum {
    GuardEventArm, /* OK - away/here */
    GuardEventMute, /* long OK */
    GuardEventDismiss, /* a key pressed while the alert is up */
    GuardEventPinNext,
    GuardEventPinPrev,
} GuardEvent;

typedef void (*GuardCallback)(void* context, GuardEvent event);

/* Everything the screen needs, handed over in one go each tick. */
typedef struct {
    CdrSnapshot snap;
    bool armed;
    bool muted;
    bool alert;
    bool fob_learned;
    uint16_t arm_in_s; /* counting down to armed, 0 when not pending */
    uint8_t pinned; /* CdrBandCount while hopping */
    uint8_t band_mask;
    uint8_t sens;
} GuardState;

GuardView* guard_view_alloc(void);
void guard_view_free(GuardView* v);
View* guard_view_get_view(GuardView* v);

void guard_view_set_callback(GuardView* v, GuardCallback cb, void* context);
void guard_view_set_state(GuardView* v, const GuardState* state);
/** Drives the alert's attention-grabbing blink and the hop marker. */
void guard_view_tick(GuardView* v);
/** Back to page 0 with the alert cleared, when a new watch begins. */
void guard_view_reset(GuardView* v);

/* ---------------- layout ----------------
 * Mirrored verbatim by tools_gen_mockups.py. */
#define G_HDR_BASE 7
#define G_RULE_Y 9
#define G_RASTER_TOP 11
#define G_RASTER_BASE 32
#define G_RASTER_H (G_RASTER_BASE - G_RASTER_TOP + 1) /* 22 */
#define G_RASTER_X0 3
#define G_RULE2_Y 33
/* Thirteen rather than nine, and the fill spans the whole interior. A chip
 * whose fill is shorter than the glyphs sitting on it renders the label half
 * inverted and half not, which is unreadable -- the mockups caught it. */
#define G_CHIP_Y 35
#define G_CHIP_H 13
#define G_CHIP_W 40
#define G_CHIP_BASE (G_CHIP_Y + 9)
#define G_TICK_Y 48
#define G_BAR_X 2
#define G_BAR_W 124
#define G_BAR_Y 50
#define G_BAR_H 5
#define G_VERDICT_BASE 63

#define G_PILL_X 39
#define G_PILL_W 30
#define G_MUTE_X 72
#define G_DOTS_X 85
#define G_PAGES 3
