#include "pomodoro_stats.h"

void pomodoro_stats_init(PomodoroStats* stats) {
    *stats = (PomodoroStats){0};
}

void pomodoro_stats_record(PomodoroStats* stats, uint32_t seconds, bool completed, uint32_t at) {
    if(seconds == 0 && !completed) return;
    uint32_t day = at / POMODORO_SECONDS_PER_DAY;

    if(!stats->has_data) {
        stats->has_data = true;
        stats->first_day = day;
        stats->today = day;
    }
    if(day < stats->first_day) stats->first_day = day;

    stats->total_seconds += seconds;
    if(completed) stats->total_pomodoros++;

    if(day > stats->today) {
        stats->today = day;
        stats->today_seconds = 0;
        stats->today_pomodoros = 0;
    }
    if(day == stats->today) {
        stats->today_seconds += seconds;
        if(completed) stats->today_pomodoros++;
        if(stats->today_seconds > stats->best_day_seconds) {
            stats->best_day_seconds = stats->today_seconds;
        }
    }

    if(completed) {
        if(stats->streak == 0 || day > stats->last_completed_day + 1) {
            stats->streak = 1;
            stats->last_completed_day = day;
        } else if(day == stats->last_completed_day + 1) {
            stats->streak++;
            stats->last_completed_day = day;
        }
        /* Same day (or an earlier day after a clock change): no change. */
        if(stats->streak > stats->best_streak) stats->best_streak = stats->streak;
    }
}

void pomodoro_stats_summary(
    const PomodoroStats* stats,
    uint32_t now,
    PomodoroStatsSummary* out) {
    uint32_t day = now / POMODORO_SECONDS_PER_DAY;
    bool is_today = stats->has_data && stats->today == day;
    uint32_t days = 0;
    if(stats->has_data) days = day >= stats->first_day ? day - stats->first_day + 1 : 1;

    *out = (PomodoroStatsSummary){
        .total_seconds = stats->total_seconds,
        .total_pomodoros = stats->total_pomodoros,
        .today_seconds = is_today ? stats->today_seconds : 0,
        .today_pomodoros = is_today ? stats->today_pomodoros : 0,
        .daily_average_seconds = days ? stats->total_seconds / days : 0,
        .best_day_seconds = stats->best_day_seconds,
        /* A streak survives until a whole day passes without a pomodoro. */
        .current_streak = (stats->streak && (stats->last_completed_day == day ||
                                             stats->last_completed_day + 1 == day)) ?
                              stats->streak :
                              0,
        .best_streak = stats->best_streak,
    };
}
