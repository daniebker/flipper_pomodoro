#include "pomodoro_fsm.h"

#include <furi.h>

/* Set to 1 at build time (e.g. temporarily edit here) to make every
 * "minute" last one second while testing. */
#ifndef POMODORO_SECONDS_PER_MINUTE
#define POMODORO_SECONDS_PER_MINUTE 60
#endif

const PomodoroModeConfig pomodoro_modes[PomodoroModeCount] = {
    [PomodoroModeShort] = {.label = "Short", .focus_min = 25, .break_min = 5, .long_break_min = 15},
    [PomodoroModeLong] = {.label = "Long", .focus_min = 50, .break_min = 10, .long_break_min = 30},
};

static uint32_t phase_ticks(const Pomodoro* p, PomodoroState phase) {
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
    return furi_ms_to_ticks(minutes * POMODORO_SECONDS_PER_MINUTE * 1000U);
}

static void enter_phase(Pomodoro* p, PomodoroState phase, uint32_t now) {
    p->state = phase;
    p->end_tick = now + phase_ticks(p, phase);
}

/* Move from a finished (or skipped) phase to the next one. */
static PomodoroAlert advance(Pomodoro* p, PomodoroState from, uint32_t now) {
    if(from == PomodoroStateFocus) {
        p->completed++;
        enter_phase(
            p,
            p->completed >= POMODORO_FOCUS_PER_CYCLE ? PomodoroStateLongBreak :
                                                       PomodoroStateShortBreak,
            now);
        return PomodoroAlertBreak;
    }
    if(from == PomodoroStateLongBreak) p->completed = 0;
    enter_phase(p, PomodoroStateFocus, now);
    return PomodoroAlertFocus;
}

void pomodoro_init(Pomodoro* p) {
    *p = (Pomodoro){.state = PomodoroStateSelect, .mode = PomodoroModeShort};
}

bool pomodoro_is_running(const Pomodoro* p) {
    return p->state == PomodoroStateFocus || p->state == PomodoroStateShortBreak ||
           p->state == PomodoroStateLongBreak;
}

PomodoroState pomodoro_phase(const Pomodoro* p) {
    return p->state == PomodoroStatePaused ? p->paused_from : p->state;
}

uint32_t pomodoro_remaining_ticks(const Pomodoro* p, uint32_t now) {
    if(p->state == PomodoroStatePaused) return p->paused_remaining;
    if(!pomodoro_is_running(p)) return 0;
    int32_t left = (int32_t)(p->end_tick - now);
    return left > 0 ? (uint32_t)left : 0;
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
            p->paused_remaining = pomodoro_remaining_ticks(p, now);
            p->paused_from = p->state;
            p->state = PomodoroStatePaused;
            break;
        case PomodoroEventSkip:
        case PomodoroEventExpired:
            return advance(p, p->state, now);
        case PomodoroEventBack:
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
            p->end_tick = now + p->paused_remaining;
            break;
        case PomodoroEventSkip:
            return advance(p, p->paused_from, now);
        case PomodoroEventBack:
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
