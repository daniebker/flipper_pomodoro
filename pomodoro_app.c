#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <furi_hal_rtc.h>
#include <stdio.h>

#include "pomodoro_fsm.h"
#include "pomodoro_storage.h"

typedef struct {
    Pomodoro fsm;
    FuriMutex* mutex; /* guards fsm: the draw callback runs on the GUI thread */
    FuriMessageQueue* input_queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notifications;
} PomodoroApp;

/* Two beeps + buzz when focus begins, one long tone when a break begins. */
static const NotificationSequence sequence_focus = {
    &message_display_backlight_on,
    &message_vibro_on,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    &message_vibro_off,
    &message_delay_100,
    &message_vibro_on,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence sequence_break = {
    &message_display_backlight_on,
    &message_vibro_on,
    &message_note_e6,
    &message_delay_500,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const char* phase_title(PomodoroState phase) {
    switch(phase) {
    case PomodoroStateFocus:
        return "Focus";
    case PomodoroStateShortBreak:
        return "Short break";
    case PomodoroStateLongBreak:
        return "Long break";
    default:
        return "";
    }
}

static void draw_select(Canvas* canvas, const Pomodoro* p) {
    char line[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Pomodoro");

    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < PomodoroModeCount; i++) {
        const PomodoroModeConfig* cfg = &pomodoro_modes[i];
        snprintf(
            line,
            sizeof(line),
            "%s: %d focus / %d rest",
            cfg->label,
            cfg->focus_min,
            cfg->break_min);
        int y = 14 + i * 16;
        if(i == (int)p->mode) {
            canvas_draw_rbox(canvas, 2, y, 124, 14, 3);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str_aligned(canvas, 64, y + 7, AlignCenter, AlignCenter, line);
        canvas_set_color(canvas, ColorBlack);
    }

    snprintf(
        line,
        sizeof(line),
        "Every %d: %d min break",
        POMODORO_FOCUS_PER_CYCLE,
        pomodoro_modes[p->mode].long_break_min);
    canvas_draw_str_aligned(canvas, 64, 47, AlignCenter, AlignTop, line);
    canvas_draw_str_aligned(canvas, 64, 64, AlignCenter, AlignBottom, "OK start   Back exit");
}

static void draw_timer(Canvas* canvas, const Pomodoro* p) {
    char buf[32];
    bool paused = p->state == PomodoroStatePaused;

    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "%s%s", phase_title(pomodoro_phase(p)), paused ? " (paused)" : "");
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, buf);

    uint32_t secs = pomodoro_remaining_seconds(p, furi_hal_rtc_get_timestamp());
    snprintf(buf, sizeof(buf), "%02lu:%02lu", secs / 60, secs % 60);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignCenter, buf);

    /* One dot per focus interval in the cycle; filled = done. */
    for(int i = 0; i < POMODORO_FOCUS_PER_CYCLE; i++) {
        int x = 64 - 18 + i * 12;
        if(i < p->completed) {
            canvas_draw_disc(canvas, x, 46, 3);
        } else {
            canvas_draw_circle(canvas, x, 46, 3);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        64,
        64,
        AlignCenter,
        AlignBottom,
        paused ? "OK resume  > skip  Back exit" : "OK pause  > skip  Back exit");
}

static void draw_callback(Canvas* canvas, void* ctx) {
    PomodoroApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(app->fsm.state == PomodoroStateSelect) {
        draw_select(canvas, &app->fsm);
    } else {
        draw_timer(canvas, &app->fsm);
    }
    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    PomodoroApp* app = ctx;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

static bool map_input(const InputEvent* input, PomodoroEvent* out) {
    if(input->type == InputTypeLong && input->key == InputKeyBack) {
        *out = PomodoroEventStop; /* hold Back: end the session */
        return true;
    }
    if(input->type != InputTypeShort) return false;
    switch(input->key) {
    case InputKeyUp:
        *out = PomodoroEventPrev;
        return true;
    case InputKeyDown:
        *out = PomodoroEventNext;
        return true;
    case InputKeyOk:
        *out = PomodoroEventConfirm;
        return true;
    case InputKeyRight:
        *out = PomodoroEventSkip;
        return true;
    case InputKeyBack:
        *out = PomodoroEventBack;
        return true;
    default:
        return false;
    }
}

int32_t pomodoro_app_app(void* p) {
    UNUSED(p);

    PomodoroApp* app = malloc(sizeof(PomodoroApp));
    pomodoro_init(&app->fsm);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    /* Resume a saved session. Its phase end time is absolute, so the time the
     * app spent closed is deducted from what was left; any phases that ended
     * meanwhile are skipped through. */
    if(pomodoro_storage_load(&app->fsm)) {
        if(pomodoro_catch_up(&app->fsm, furi_hal_rtc_get_timestamp()) != PomodoroAlertNone) {
            pomodoro_storage_save(&app->fsm);
        }
    }

    bool running = true;
    while(running) {
        InputEvent input;
        PomodoroEvent event;
        /* Wake at least every 250 ms to refresh the countdown and check expiry. */
        bool got_input = furi_message_queue_get(app->input_queue, &input, 250) == FuriStatusOk;
        bool have_event = got_input && map_input(&input, &event);

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        uint32_t now = furi_hal_rtc_get_timestamp();
        PomodoroAlert alert = PomodoroAlertNone;
        bool leave = false;
        if(have_event && event == PomodoroEventBack && pomodoro_has_session(&app->fsm)) {
            /* Back during a session leaves the app; the session keeps going. */
            leave = true;
        } else if(have_event) {
            PomodoroAlert a = pomodoro_dispatch(&app->fsm, event, now);
            if(a != PomodoroAlertNone) alert = a;
        }
        PomodoroAlert expired = pomodoro_catch_up(&app->fsm, now);
        if(expired != PomodoroAlertNone) alert = expired;
        bool changed = (have_event && !leave) || alert != PomodoroAlertNone;
        running = !leave && app->fsm.state != PomodoroStateExit;
        if(changed) pomodoro_storage_save(&app->fsm);
        furi_mutex_release(app->mutex);

        if(alert == PomodoroAlertFocus) {
            notification_message(app->notifications, &sequence_focus);
        } else if(alert == PomodoroAlertBreak) {
            notification_message(app->notifications, &sequence_break);
        }
        view_port_update(app->view_port);
    }

    pomodoro_storage_save(&app->fsm);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
