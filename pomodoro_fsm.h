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
    PomodoroEventBack, /* timer: back to menu; menu: exit */
    PomodoroEventExpired, /* timer reached zero */
} PomodoroEvent;

/* What the UI should announce after a transition. */
typedef enum {
    PomodoroAlertNone,
    PomodoroAlertFocus,
    PomodoroAlertBreak,
} PomodoroAlert;

typedef struct {
    PomodoroState state;
    PomodoroState paused_from; /* phase to resume into */
    PomodoroMode mode;
    uint8_t completed; /* focus intervals finished in this cycle */
    uint32_t end_tick; /* when the running phase ends */
    uint32_t paused_remaining; /* ticks left while paused */
} Pomodoro;

void pomodoro_init(Pomodoro* p);

/* The single place where state transitions happen. `now` is furi_get_tick(). */
PomodoroAlert pomodoro_dispatch(Pomodoro* p, PomodoroEvent event, uint32_t now);

/* True while a timed phase is counting down (not paused). */
bool pomodoro_is_running(const Pomodoro* p);

/* The timed phase being shown, looking through Paused. */
PomodoroState pomodoro_phase(const Pomodoro* p);

uint32_t pomodoro_remaining_ticks(const Pomodoro* p, uint32_t now);
