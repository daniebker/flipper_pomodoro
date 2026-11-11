#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <furi_hal_rtc.h>
#include <stdio.h>

#include "pomodoro_fsm.h"
#include "pomodoro_stats.h"
#include "pomodoro_storage.h"

typedef struct {
    Pomodoro fsm;
    PomodoroStats stats;
    bool stats_dirty; /* stats changed and need saving */
    bool show_stats; /* stats page is on screen */
    bool exit_menu_open; /* "leave the app?" menu is on screen */
    uint8_t exit_choice; /* ExitChoice highlighted in that menu */
    FuriMutex* mutex; /* guards the fields above: drawing runs on the GUI thread */
    FuriMessageQueue* input_queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notifications;
} PomodoroApp;

/* Two beeps + buzz when focus is up next, one long tone when a break is. */
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

typedef enum {
    ExitChoiceContinue, /* leave the app, session keeps running */
    ExitChoiceStop, /* end the session and close the app */
    ExitChoiceCount,
} ExitChoice;

static const char* const exit_choice_labels[ExitChoiceCount] = {
    [ExitChoiceContinue] = "Continue tracking",
    [ExitChoiceStop] = "Stop tracking & exit",
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
    canvas_draw_str_aligned(canvas, 64, 64, AlignCenter, AlignBottom, "OK start   < stats");
}

static void draw_timer(Canvas* canvas, const Pomodoro* p) {
    char buf[32];
    bool paused = p->state == PomodoroStatePaused;
    bool ready = p->state == PomodoroStateReady;

    canvas_set_font(canvas, FontPrimary);
    snprintf(
        buf,
        sizeof(buf),
        "%s%s%s",
        ready ? "Up next: " : "",
        phase_title(pomodoro_phase(p)),
        paused ? " (paused)" : "");
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
        ready  ? "OK start  > skip  < stats" :
        paused ? "OK resume  > skip  < stats" :
                 "OK pause  > skip  < stats");
}

static void draw_exit_menu(Canvas* canvas, uint8_t choice) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Leave Pomodoro?");

    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < ExitChoiceCount; i++) {
        int y = 15 + i * 16;
        if(i == choice) {
            canvas_draw_rbox(canvas, 2, y, 124, 14, 3);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str_aligned(canvas, 64, y + 7, AlignCenter, AlignCenter, exit_choice_labels[i]);
        canvas_set_color(canvas, ColorBlack);
    }
    canvas_draw_str_aligned(canvas, 64, 64, AlignCenter, AlignBottom, "OK select   Back cancel");
}

static void format_duration(char* buf, size_t size, uint32_t seconds) {
    uint32_t minutes = seconds / 60;
    if(minutes >= 60) {
        snprintf(buf, size, "%luh %02lum", minutes / 60, minutes % 60);
    } else {
        snprintf(buf, size, "%lum", minutes);
    }
}

static void draw_stat_row(Canvas* canvas, int row, const char* label, const char* value) {
    int y = 21 + row * 10;
    canvas_draw_str(canvas, 2, y, label);
    canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, value);
}

static void draw_stats(Canvas* canvas, const PomodoroStats* stats) {
    PomodoroStatsSummary sum;
    pomodoro_stats_summary(stats, furi_hal_rtc_get_timestamp(), &sum);
    char dur[16];
    char value[32];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Stats");
    canvas_set_font(canvas, FontSecondary);

    format_duration(dur, sizeof(dur), sum.total_seconds);
    snprintf(value, sizeof(value), "%s (%lu)", dur, sum.total_pomodoros);
    draw_stat_row(canvas, 0, "Total", value);

    format_duration(dur, sizeof(dur), sum.today_seconds);
    snprintf(value, sizeof(value), "%s (%lu)", dur, sum.today_pomodoros);
    draw_stat_row(canvas, 1, "Today", value);

    format_duration(value, sizeof(value), sum.daily_average_seconds);
    draw_stat_row(canvas, 2, "Daily average", value);

    format_duration(value, sizeof(value), sum.best_day_seconds);
    draw_stat_row(canvas, 3, "Best day", value);

    snprintf(
        value,
        sizeof(value),
        "%lu day%s (best %lu)",
        sum.current_streak,
        sum.current_streak == 1 ? "" : "s",
        sum.best_streak);
    draw_stat_row(canvas, 4, "Streak", value);
}

static void draw_callback(Canvas* canvas, void* ctx) {
    PomodoroApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    canvas_clear(canvas);
    if(app->exit_menu_open) {
        draw_exit_menu(canvas, app->exit_choice);
    } else if(app->show_stats) {
        draw_stats(canvas, &app->stats);
    } else if(app->fsm.state == PomodoroStateSelect) {
        draw_select(canvas, &app->fsm);
    } else {
        draw_timer(canvas, &app->fsm);
    }
    furi_mutex_release(app->mutex);
}

static void on_focus(void* ctx, uint32_t seconds, bool completed, uint32_t at) {
    PomodoroApp* app = ctx;
    pomodoro_stats_record(&app->stats, seconds, completed, at);
    app->stats_dirty = true;
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
    pomodoro_set_focus_callback(&app->fsm, on_focus, app);
    pomodoro_storage_load_stats(&app->stats);
    app->stats_dirty = false;
    app->show_stats = false;
    app->exit_menu_open = false;
    app->exit_choice = ExitChoiceContinue;
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
    /* Focus intervals that finished while the app was closed count too. */
    if(app->stats_dirty) {
        pomodoro_storage_save_stats(&app->stats);
        app->stats_dirty = false;
    }

    bool running = true;
    while(running) {
        InputEvent input;
        PomodoroEvent event;
        /* Wake at least every 250 ms to refresh the countdown and check expiry. */
        bool got_input = furi_message_queue_get(app->input_queue, &input, 250) == FuriStatusOk;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        /* The stats page sits on top of the menu or timer, which keeps running
         * underneath. Left opens it; Left, Back or OK closes it. */
        bool short_press = got_input && input.type == InputTypeShort;
        bool leave = false; /* close the app, session keeps running */
        bool stop_and_exit = false; /* end the session, then close the app */
        if(app->exit_menu_open) {
            if(short_press) {
                switch(input.key) {
                case InputKeyUp:
                case InputKeyDown:
                    app->exit_choice = (app->exit_choice + 1) % ExitChoiceCount;
                    break;
                case InputKeyOk:
                    app->exit_menu_open = false;
                    if(app->exit_choice == ExitChoiceStop) {
                        stop_and_exit = true;
                    } else {
                        leave = true;
                    }
                    break;
                case InputKeyBack:
                    app->exit_menu_open = false; /* cancel: back to the timer */
                    break;
                default:
                    break;
                }
            }
            got_input = false;
        } else if(app->show_stats) {
            if(short_press && (input.key == InputKeyLeft || input.key == InputKeyBack ||
                               input.key == InputKeyOk)) {
                app->show_stats = false;
            }
            got_input = false;
        } else if(short_press && input.key == InputKeyLeft) {
            app->show_stats = true;
            got_input = false;
        }
        bool have_event = got_input && map_input(&input, &event);

        uint32_t now = furi_hal_rtc_get_timestamp();
        PomodoroAlert alert = PomodoroAlertNone;
        if(have_event && event == PomodoroEventBack && pomodoro_has_session(&app->fsm)) {
            /* Back during a session asks whether to keep tracking. */
            app->exit_menu_open = true;
            app->exit_choice = ExitChoiceContinue;
            have_event = false;
        } else if(stop_and_exit) {
            /* Records any partial focus time in the stats and ends the
             * session, so the saved session file is removed on exit. */
            pomodoro_dispatch(&app->fsm, PomodoroEventStop, now);
        } else if(have_event) {
            PomodoroAlert a = pomodoro_dispatch(&app->fsm, event, now);
            if(a != PomodoroAlertNone) alert = a;
        }
        PomodoroAlert expired = pomodoro_catch_up(&app->fsm, now);
        if(expired != PomodoroAlertNone) alert = expired;
        bool changed = have_event || stop_and_exit || alert != PomodoroAlertNone;
        running = !leave && !stop_and_exit && app->fsm.state != PomodoroStateExit;
        if(changed) pomodoro_storage_save(&app->fsm);
        if(app->stats_dirty) {
            pomodoro_storage_save_stats(&app->stats);
            app->stats_dirty = false;
        }
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
