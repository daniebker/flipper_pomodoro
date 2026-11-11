#include "pomodoro_fsm.h"

/* Set to 1 to make every "minute" last one second while testing. */
#ifndef POMODORO_SECONDS_PER_MINUTE
#define POMODORO_SECONDS_PER_MINUTE 60
#endif

const PomodoroModeConfig pomodoro_modes[PomodoroModeCount] = {
    [PomodoroModeShort] = {.label = "Short", .focus_min = 25, .break_min = 5, .long_break_min = 15},
    [PomodoroModeLong] = {.label = "Long", .focus_min = 50, .break_min = 10, .long_break_min = 30},
};

static bool is_timed_phase(PomodoroState s) {
    return s == PomodoroStateFocus || s == PomodoroStateShortBreak ||
           s == PomodoroStateLongBreak;
}

static uint32_t phase_seconds(const Pomodoro* p, PomodoroState phase) {
    const PomodoroModeConfig* cfg = &pomodoro_modes[p->mode];
    uint32_t minutes = 0;
    switch(phase) {
    case PomodoroStateFocus:
        minutes = cfg->focus_min;
        break;
    case PomodoroStateShortBreak:
        minutes = cfg->break_min;
        break;
    case PomodoroStateLongBreak:
        minutes = cfg->long_break_min;
        break;
    default:
        break;
    }
    return minutes * POMODORO_SECONDS_PER_MINUTE;
}

static void enter_phase(Pomodoro* p, PomodoroState phase, uint32_t start) {
    p->state = phase;
    p->end_time = start + phase_seconds(p, phase);
}

/* Move from a finished (or skipped) phase to the next one, starting at `start`. */
static PomodoroAlert advance(Pomodoro* p, PomodoroState from, uint32_t start) {
    if(from == PomodoroStateFocus) {
        p->completed++;
        enter_phase(
            p,
            p->completed >= POMODORO_FOCUS_PER_CYCLE ? PomodoroStateLongBreak :
                                                       PomodoroStateShortBreak,
            start);
        return PomodoroAlertBreak;
    }
    if(from == PomodoroStateLongBreak) p->completed = 0;
    enter_phase(p, PomodoroStateFocus, start);
    return PomodoroAlertFocus;
}

void pomodoro_init(Pomodoro* p) {
    *p = (Pomodoro){.state = PomodoroStateSelect, .mode = PomodoroModeShort};
}

bool pomodoro_is_running(const Pomodoro* p) {
    return is_timed_phase(p->state);
}

bool pomodoro_has_session(const Pomodoro* p) {
    return pomodoro_is_running(p) || p->state == PomodoroStatePaused;
}

bool pomodoro_is_valid_session(const Pomodoro* p) {
    if(p->mode >= PomodoroModeCount || p->completed > POMODORO_FOCUS_PER_CYCLE) return false;
    if(pomodoro_is_running(p)) return true;
    return p->state == PomodoroStatePaused && is_timed_phase(p->paused_from) &&
           p->paused_remaining <= phase_seconds(p, p->paused_from);
}

PomodoroState pomodoro_phase(const Pomodoro* p) {
    return p->state == PomodoroStatePaused ? p->paused_from : p->state;
}

uint32_t pomodoro_remaining_seconds(const Pomodoro* p, uint32_t now) {
    if(p->state == PomodoroStatePaused) return p->paused_remaining;
    if(!pomodoro_is_running(p)) return 0;
    int32_t left = (int32_t)(p->end_time - now);
    return left > 0 ? (uint32_t)left : 0;
}

PomodoroAlert pomodoro_catch_up(Pomodoro* p, uint32_t now) {
    if(!pomodoro_is_running(p)) return PomodoroAlertNone;

    /* If the clock was set backwards, don't show more time than a full phase. */
    uint32_t duration = phase_seconds(p, p->state);
    if(pomodoro_remaining_seconds(p, now) > duration) p->end_time = now + duration;

    PomodoroAlert alert = PomodoroAlertNone;
    while(pomodoro_is_running(p) && pomodoro_remaining_seconds(p, now) == 0) {
        alert = pomodoro_dispatch(p, PomodoroEventExpired, now);
    }
    return alert;
}

PomodoroAlert pomodoro_dispatch(Pomodoro* p, PomodoroEvent event, uint32_t now) {
    switch(p->state) {
    case PomodoroStateSelect:
        switch(event) {
        case PomodoroEventPrev:
            p->mode = (p->mode + PomodoroModeCount - 1) % PomodoroModeCount;
            break;
        case PomodoroEventNext:
            p->mode = (p->mode + 1) % PomodoroModeCount;
            break;
        case PomodoroEventConfirm:
            p->completed = 0;
            enter_phase(p, PomodoroStateFocus, now);
            break;
        case PomodoroEventBack:
            p->state = PomodoroStateExit;
            break;
        default:
            break;
        }
        break;

    case PomodoroStateFocus:
    case PomodoroStateShortBreak:
    case PomodoroStateLongBreak:
        switch(event) {
        case PomodoroEventConfirm:
            p->paused_remaining = pomodoro_remaining_seconds(p, now);
            p->paused_from = p->state;
            p->state = PomodoroStatePaused;
            break;
        case PomodoroEventSkip:
            return advance(p, p->state, now);
        case PomodoroEventExpired:
            /* Chain from the scheduled end, not `now`, so catching up after the
             * app was closed keeps every later phase on its real schedule. */
            return advance(p, p->state, p->end_time);
        case PomodoroEventStop:
            p->state = PomodoroStateSelect;
            break;
        default:
            break;
        }
        break;

    case PomodoroStatePaused:
        switch(event) {
        case PomodoroEventConfirm:
            p->state = p->paused_from;
            p->end_time = now + p->paused_remaining;
            break;
        case PomodoroEventSkip:
            return advance(p, p->paused_from, now);
        case PomodoroEventStop:
            p->state = PomodoroStateSelect;
            break;
        default:
            break;
        }
        break;

    case PomodoroStateExit:
        break;
    }
    return PomodoroAlertNone;
}
