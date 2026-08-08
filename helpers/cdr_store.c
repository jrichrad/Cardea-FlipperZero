#include "cdr_store.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include <stdio.h>
#include <string.h>

#define CDR_SETTINGS_PATH APP_DATA_PATH("settings.bin")
#define CDR_LOG_PATH APP_DATA_PATH("events.csv")
#define CDR_SETTINGS_MAGIC 0xCD
#define CDR_SETTINGS_VERSION 1

const char* const cdr_log_path_pretty = "/ext/apps_data/cardea/events.csv";

/* A watchdog is only any use if it is already watching when you walk away, so
 * the delay exists to cover the walk itself. Instant is for setting it down
 * inside a car you are leaving on foot. */
const uint8_t cdr_arm_delay_seconds[CDR_ARM_DELAY_COUNT] = {0, 10, 30, 60};
const char* const cdr_arm_delay_labels[CDR_ARM_DELAY_COUNT] = {"Instant", "10 s", "30 s", "60 s"};

void cdr_store_defaults(CardeaSettings* s) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    /* Every band, because the user should not have to know what their car
     * speaks before the app will listen for it. */
    s->band_mask = (uint8_t)((1u << CdrBandCount) - 1u);
    s->sens = CdrSensNormal;
    s->arm_delay = 1; /* 10 s */
    s->sound = true;
    s->vibro = true;
    s->led = true;
    s->log_csv = true;
    s->keep_awake = true;
}

static void cdr_store_ensure_dir(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, STORAGE_APP_DATA_PATH_PREFIX);
    furi_record_close(RECORD_STORAGE);
}

void cdr_store_save(const CardeaSettings* s) {
    furi_assert(s);
    cdr_store_ensure_dir();
    saved_struct_save(
        CDR_SETTINGS_PATH, s, sizeof(CardeaSettings), CDR_SETTINGS_MAGIC, CDR_SETTINGS_VERSION);
}

void cdr_store_load(CardeaSettings* s) {
    furi_assert(s);
    CardeaSettings loaded;
    if(!saved_struct_load(
           CDR_SETTINGS_PATH,
           &loaded,
           sizeof(CardeaSettings),
           CDR_SETTINGS_MAGIC,
           CDR_SETTINGS_VERSION)) {
        return; /* nothing valid on disk - the caller keeps its defaults */
    }

    /* Never let a file on the SD card index an array, and never let it leave
     * the app watching no bands at all. */
    if(loaded.sens >= CdrSensCount) loaded.sens = CdrSensNormal;
    if(loaded.arm_delay >= CDR_ARM_DELAY_COUNT) loaded.arm_delay = 1;
    loaded.band_mask &= (uint8_t)((1u << CdrBandCount) - 1u);
    if(loaded.band_mask == 0) loaded.band_mask = (uint8_t)((1u << CdrBandCount) - 1u);
    if(loaded.fob.band >= CdrBandCount) loaded.fob.learned = false;
    if(loaded.fob.dur_ms == 0) loaded.fob.learned = false;

    *s = loaded;
}

void cdr_store_log_event(const CdrSnapshot* snap) {
    if(!snap) return;

    cdr_store_ensure_dir();
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    bool fresh = !storage_file_exists(storage, CDR_LOG_PATH);

    if(storage_file_open(file, CDR_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char line[160];

        if(fresh) {
            const char* header =
                "watch_s,band_mhz,verdict,score,matched,total,period_ms,jitter_pct,clone_pct,held_band\n";
            storage_file_write(file, header, strlen(header));
        }

        const CdrVerdict* v = &snap->verdict;
        uint8_t band = v->band < CdrBandCount ? v->band : CdrBand433;
        uint8_t level = v->level < CdrLevelCount ? v->level : 0;

        /* Clamp before formatting: -Werror=format-truncation cannot prove a
         * width it has not been shown. */
        uint32_t watch_s = snap->elapsed_ms / 1000u;
        if(watch_s > 999999u) watch_s = 999999u;
        uint16_t jitter = v->jitter_pct > 999u ? 999u : v->jitter_pct;
        uint8_t clone = v->clone_pct == CDR_PCT_NA ? 0 : v->clone_pct;

        int n = snprintf(
            line,
            sizeof(line),
            "%lu,%s,%s,%u,%u,%u,%u,%u,%u,%u\n",
            (unsigned long)watch_s,
            cdr_bands[band].label,
            cdr_level_labels[level],
            (unsigned)v->score,
            (unsigned)v->matched,
            (unsigned)v->total,
            (unsigned)v->period_ms,
            (unsigned)jitter,
            (unsigned)clone,
            (unsigned)(snap->link.active ? 1 : 0));

        if(n > 0) storage_file_write(file, line, (uint16_t)n);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}
