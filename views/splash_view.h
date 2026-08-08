/* The opening title. A door closing on its hinge, then the wordmark.
 *
 * Cardea is the Roman goddess of the door hinge -- the one who keeps at the
 * threshold what belongs outside it. The intro is that, and nothing else.
 */
#pragma once

#include <gui/view.h>

typedef struct SplashView SplashView;

typedef void (*SplashSkipCallback)(void* context);

SplashView* splash_view_alloc(void);
void splash_view_free(SplashView* v);
View* splash_view_get_view(SplashView* v);

void splash_view_set_skip_callback(SplashView* v, SplashSkipCallback cb, void* context);
/** 0..100. Drives the whole animation; nothing here keeps its own clock. */
void splash_view_set_progress(SplashView* v, uint8_t progress);
