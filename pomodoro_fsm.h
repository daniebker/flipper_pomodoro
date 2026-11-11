#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Number of focus intervals before a long break. */
#define POMODORO_FOCUS_PER_CYCLE 4

typedef enum {
    PomodoroModeShort, /* 25 min focus / 5 min rest */
    PomodoroModeLong, /* 50 min focus / 10 min rest */
    PomodoroModeCount,
} PomodoroMode;

typedef struct {
    const char* label;
    uint8_t focus_min;
    uint8_t break_min;
    uint8_t long_break_min;
} PomodoroModeConfig;

extern const PomodoroModeConfig pomodoro_modes[PomodoroModeCount];

typedef enum {
    PomodoroStateSelect, /* choosing short or long sessions */
    PomodoroStateFocus,
    PomodoroStateShortBreak,
    PomodoroStateLongBreak,
    PomodoroStatePaused, /* any timed phase, frozen */
    PomodoroStateExit,
} PomodoroState;

typedef enum {
    PomodoroEventPrev, /* menu: previous option */
    PomodoroEventNext, /* menu: next option */
    PomodoroEventConfirm, /* menu: start; timer: pause/resume */
    PomodoroEventSkip, /* timer: end current phase now */
    PomodoroEventStop, /* timer: end the session, back to menu */
    PomodoroEventBack, /* menu: exit the app */
    PomodoroEventExpired, /* timer reached zero */
} PomodoroEvent;

/* What the UI should announce after a transition. */
typedef enum {
    PomodoroAlertNone,
    PomodoroAlertFocus,
    PomodoroAlertBreak,
} PomodoroAlert;

/* Called whenever a stretch of focus ends: `seconds` actually focused (pauses
 * excluded), `completed` if the interval ran to the end, `at` = when it ended. */
typedef void (*PomodoroFocusCallback)(void* context, uint32_t seconds, bool completed, uint32_t at);

/* All times are real-time-clock timestamps in seconds, so a session can be
 * saved, the app closed, and the countdown resumed later. */
typedef struct {
    PomodoroState state;
    PomodoroState paused_from; /* phase to resume into */
    PomodoroMode mode;
    uint8_t completed; /* focus intervals finished in this cycle */
    uint32_t end_time; /* when the running phase ends */
    uint32_t paused_remaining; /* seconds left while paused */
    PomodoroFocusCallback focus_callback;
    void* focus_context;
} Pomodoro;

void pomodoro_init(Pomodoro* p);

void pomodoro_set_focus_callback(Pomodoro* p, PomodoroFocusCallback callback, void* context);

/* The single place where state transitions happen. `now` is the RTC timestamp. */
PomodoroAlert pomodoro_dispatch(Pomodoro* p, PomodoroEvent event, uint32_t now);

/* Advance through every phase that has ended by `now` (e.g. while the app was
 * closed). Returns the alert for the last transition, or None. */
PomodoroAlert pomodoro_catch_up(Pomodoro* p, uint32_t now);

/* True while a timed phase is counting down (not paused). */
bool pomodoro_is_running(const Pomodoro* p);

/* True if there is a session in progress (running or paused) worth saving. */
bool pomodoro_has_session(const Pomodoro* p);

/* Sanity-check a session loaded from storage. */
bool pomodoro_is_valid_session(const Pomodoro* p);

/* The timed phase being shown, looking through Paused. */
PomodoroState pomodoro_phase(const Pomodoro* p);

uint32_t pomodoro_remaining_seconds(const Pomodoro* p, uint32_t now);
