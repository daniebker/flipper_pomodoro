# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-11-11

### Added

- Pomodoro timer app for Flipper Zero, driven by a finite state machine.
- Session mode menu: Short (25 min focus / 5 min rest) and Long (50 min focus / 10 min rest).
- Long break after every fourth focus interval: 15 min in Short mode, 30 min in Long mode.
- Pause/resume (OK), skip to next phase (Right) and return to menu (Back) during a session.
- Countdown display with progress dots for the current cycle.
- Vibration, sound and backlight alerts when focus or a break begins.
- README and MIT license.

[Unreleased]: https://github.com/OWNER/pomodoro_app/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/OWNER/pomodoro_app/releases/tag/v1.0.0
