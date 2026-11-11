#include "pomodoro_storage.h"

#include <furi.h>
#include <storage/storage.h>

#define SESSION_DIR     EXT_PATH("apps_data/pomodoro_app")
#define SESSION_PATH    SESSION_DIR "/session.bin"
#define SESSION_MAGIC   0x4D4F504FU /* "POMO" */
#define SESSION_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t state;
    uint8_t paused_from;
    uint8_t mode;
    uint8_t completed;
    uint32_t end_time;
    uint32_t paused_remaining;
} SavedSession;

bool pomodoro_storage_load(Pomodoro* p) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    SavedSession saved;
    bool loaded = false;

    if(storage_file_open(file, SESSION_PATH, FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_read(file, &saved, sizeof(saved)) == sizeof(saved) &&
       saved.magic == SESSION_MAGIC && saved.version == SESSION_VERSION) {
        Pomodoro session = {
            .state = (PomodoroState)saved.state,
            .paused_from = (PomodoroState)saved.paused_from,
            .mode = (PomodoroMode)saved.mode,
            .completed = saved.completed,
            .end_time = saved.end_time,
            .paused_remaining = saved.paused_remaining,
        };
        if(pomodoro_is_valid_session(&session)) {
            *p = session;
            loaded = true;
        }
    }

    storage_file_free(file); /* also closes it */
    furi_record_close(RECORD_STORAGE);
    return loaded;
}

void pomodoro_storage_save(const Pomodoro* p) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    if(pomodoro_has_session(p)) {
        storage_simply_mkdir(storage, EXT_PATH("apps_data"));
        storage_simply_mkdir(storage, SESSION_DIR);

        SavedSession saved = {
            .magic = SESSION_MAGIC,
            .version = SESSION_VERSION,
            .state = (uint8_t)p->state,
            .paused_from = (uint8_t)p->paused_from,
            .mode = (uint8_t)p->mode,
            .completed = p->completed,
            .end_time = p->end_time,
            .paused_remaining = p->paused_remaining,
        };
        File* file = storage_file_alloc(storage);
        if(storage_file_open(file, SESSION_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(file, &saved, sizeof(saved));
        }
        storage_file_free(file);
    } else {
        storage_simply_remove(storage, SESSION_PATH);
    }

    furi_record_close(RECORD_STORAGE);
}
