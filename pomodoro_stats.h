#pragma once

#include <stdbool.h>
#include <stdint.h>

#define POMODORO_SECONDS_PER_DAY 86400U

/* Lifetime focus statistics. Days are counted from the Flipper's RTC, which
 * keeps local time, so day boundaries are local midnight. */
typedef struct {
    bool has_data;
    uint32_t total_seconds; /* all focused time, including partial intervals */
    uint32_t total_pomodoros; /* focus intervals that ran to the end */
    uint32_t first_day; /* day of the first recorded focus time */
    uint32_t today; /* day the today_* fields belong to */
    uint32_t today_seconds;
    uint32_t today_pomodoros;
    uint32_t best_day_seconds;
    uint32_t last_completed_day; /* last day with a finished pomodoro */
    uint32_t streak; /* consecutive days with a finished pomodoro */
    uint32_t best_streak;
} PomodoroStats;

/* What the stats page shows, computed for a given moment. */
typedef struct {
    uint32_t total_seconds;
    uint32_t total_pomodoros;
    uint32_t today_seconds;
    uint32_t today_pomodoros;
    uint32_t daily_average_seconds;
    uint32_t best_day_seconds;
    uint32_t current_streak;
    uint32_t best_streak;
} PomodoroStatsSummary;

void pomodoro_stats_init(PomodoroStats* stats);

/* Record `seconds` of focus that ended at RTC timestamp `at`. `completed` is
 * true when the focus interval ran to the end (not skipped or stopped). */
void pomodoro_stats_record(PomodoroStats* stats, uint32_t seconds, bool completed, uint32_t at);

void pomodoro_stats_summary(
    const PomodoroStats* stats,
    uint32_t now,
    PomodoroStatsSummary* out);
