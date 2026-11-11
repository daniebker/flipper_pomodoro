# Pomodoro for Flipper Zero

A simple Pomodoro timer for the Flipper Zero. Pick a session length, then work in focus intervals separated by short breaks, with a longer break after every fourth focus interval.

## Session modes

| Mode  | Focus  | Short break | Long break (after 4 focus intervals) |
|-------|--------|-------------|--------------------------------------|
| Short | 25 min | 5 min       | 15 min                               |
| Long  | 50 min | 10 min      | 30 min                               |

When a phase ends, the screen lights up and the Flipper vibrates and beeps: two short beeps when focus starts, one longer tone when a break starts.

## Controls

**Mode menu**

| Button      | Action             |
|-------------|--------------------|
| Up / Down   | Choose Short or Long |
| OK          | Start the session  |
| Back        | Exit the app       |

**Timer**

| Button | Action                                  |
|--------|-----------------------------------------|
| OK     | Pause / resume                          |
| Right  | Skip to the next phase (counts as done) |
| Back   | Stop and return to the mode menu        |

The four dots under the countdown show how many focus intervals you have finished in the current cycle.

## Building

Requires [uFBT](https://github.com/flipperdevices/flipperzero-ufbt).

```sh
ufbt            # build dist/pomodoro_app.fap
ufbt launch     # build, install and run on a connected Flipper
```

The app appears under **Apps → Tools → Pomodoro**.

### Faster testing

In `pomodoro_fsm.c`, set `POMODORO_SECONDS_PER_MINUTE` to `1` so every "minute" lasts one second. Set it back to `60` before real use.

## How it works

The app is driven by a finite state machine in `pomodoro_fsm.c`. All transitions happen in `pomodoro_dispatch()`; `pomodoro_app.c` only turns button presses into events, draws the current state and plays alerts.

```
Select ──OK──▶ Focus ──done──▶ Short break ──done──▶ Focus …
                 │                                      
                 └─4th done──▶ Long break ──done──▶ Focus (cycle resets)

Any timed phase ──OK──▶ Paused ──OK──▶ same phase
Any timed phase / Paused ──Back──▶ Select ──Back──▶ Exit
```

Durations live in the `pomodoro_modes` table in `pomodoro_fsm.c` if you want to change them.

## License

[MIT](LICENSE) © 2026 Daniel Baker
