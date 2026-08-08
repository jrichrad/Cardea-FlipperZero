/* What the last watch saw.
 *
 * The alert is for the moment; the report is for the morning after, when the
 * question is "did anything happen while I was asleep" and nobody was there to
 * watch the screen.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/cdr_radio.h"

typedef struct ReportView ReportView;

typedef struct {
    CdrSnapshot snap;
    bool valid; /* false before the first watch */
    bool fob_learned;
    bool logged;
} ReportState;

ReportView* report_view_alloc(void);
void report_view_free(ReportView* v);
View* report_view_get_view(ReportView* v);

void report_view_set_state(ReportView* v, const ReportState* state);
