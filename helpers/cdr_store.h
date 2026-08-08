/* Settings, the learned key, and the alert log.
 *
 * The learned key rides along inside the settings struct rather than in a file
 * of its own: it is thirteen bytes, it is meaningless without the band mask and
 * sensitivity it was captured under, and one saved_struct cannot half-load.
 */
#pragma once

#include "cdr_detect.h"
#include "cdr_radio.h"

#define CDR_ARM_DELAY_COUNT 4
extern const uint8_t cdr_arm_delay_seconds[CDR_ARM_DELAY_COUNT];
extern const char* const cdr_arm_delay_labels[CDR_ARM_DELAY_COUNT];

typedef struct {
    uint8_t band_mask; /* bit per CdrBandId */
    uint8_t sens; /* CdrSens */
    uint8_t arm_delay; /* index into cdr_arm_delay_seconds */
    bool sound;
    bool vibro;
    bool led;
    bool log_csv;
    bool keep_awake;
    CdrFob fob;
} CardeaSettings;

void cdr_store_defaults(CardeaSettings* s);
void cdr_store_save(const CardeaSettings* s);
void cdr_store_load(CardeaSettings* s);

/** Append one line to /ext/apps_data/cardea/events.csv. Called when the
 *  verdict climbs, so the log is a record of moments rather than a firehose. */
void cdr_store_log_event(const CdrSnapshot* snap);

/** Path shown in the About screen, so the operator can find the log. */
extern const char* const cdr_log_path_pretty;
