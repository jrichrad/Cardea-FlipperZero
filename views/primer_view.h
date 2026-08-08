/* How relay theft actually works, drawn rather than written.
 *
 * This screen exists because the app makes a claim that sounds wrong the first
 * time you hear it -- that a Flipper parked in a car can see a keyless theft
 * happening thirty metres away, through a wall, without hearing the part of
 * the attack that is being relayed. Six panels, animated, explaining why that
 * is true and where it stops being true.
 */
#pragma once

#include <gui/view.h>

#define PRIMER_PANELS 6

typedef struct PrimerView PrimerView;

PrimerView* primer_view_alloc(void);
void primer_view_free(PrimerView* v);
View* primer_view_get_view(PrimerView* v);

/** One animation frame; also drives the automatic page turn. */
void primer_view_tick(PrimerView* v);
void primer_view_reset(PrimerView* v);
