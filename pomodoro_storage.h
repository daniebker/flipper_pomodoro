#pragma once

#include "pomodoro_fsm.h"

/* Load a saved session into `p`. Returns false (leaving `p` untouched) if
 * there is no saved session or it is unreadable. */
bool pomodoro_storage_load(Pomodoro* p);

/* Save the session if one is in progress; otherwise delete any saved one. */
void pomodoro_storage_save(const Pomodoro* p);
