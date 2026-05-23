# Plan: Qt-Free `dddcli` Capture CLI

## Summary
Create a Linux-first CLI version of the Domesday Duplicator capture app under `tools/cli-app`, named `dddcli`. Base the work only on `tools/DomesdayDuplicator/`; ignore `tools/dddconv` and `tools/dddutil`. The new CLI will have no Qt dependency, support manual USB capture plus automatic capture with serial player control, use a minimal TOML config reader, optionally write JSON sidecar metadata with `--json <file>`, and write this plan to `tools/cli-app/IMPLEMENTATION_PLAN.md`.

## Public Interface
- Add subcommands:
  - `dddcli list-devices`: list detected Domesday USB capture devices.
  - `dddcli capture`: manual USB capture.
  - `dddcli auto-capture`: serial-controlled automatic capture.
  - `dddcli player`: basic player status/control for setup and debugging.
- Add common options:
  - `--config <file>`, defaulting to `~/.config/domesday-duplicator/dddcli.toml`.
  - CLI flags override TOML values.
  - `--debug`, `--quiet`, and progress/status output to stderr.
- Add capture options:
  - `--output <file>` optional. If omitted, generate a GUI-like timestamped name.
  - `--json <file>` optional. If supplied, write JSON sidecar metadata after capture finalization.
  - `--output-dir <dir>` from config or current directory.
  - `--format lds|raw|cds`.
  - `--test-mode`, USB preferred device path, VID/PID, small-transfer settings, and queue sizes.
- Add automatic capture options:
  - `--serial-device <path>`, `--serial-speed auto|9600|4800|2400|1200`.
  - `--disc-type cav|clv`.
  - `--mode whole-disc|lead-in|partial`.
  - `--start-address <n>` and `--end-address <n>` for partial capture.
  - `--key-lock`.

## Implementation Changes
- Create `tools/cli-app` with its own CMake target `dddcli`, requiring C++20 and LibUSB only.
- Add `tools/cli-app/IMPLEMENTATION_PLAN.md` containing this plan for future reference.
- Use `tools/DomesdayDuplicator/` as the source of truth for capture behavior.
- Reuse or extract the Qt-free USB capture core where practical: `UsbDeviceBase`, `UsbDeviceLibUsb`, `ILogger`, and a new non-Qt console logger.
- Do not depend on or refactor `dddconv` or `dddutil`.
- Exclude GUI-only files, Qt logger, QCustomPlot, dialogs, `Configuration`, `MainWindow`, and Qt player classes from the CLI build.
- Replace QtSerialPort/player code with Linux POSIX `termios` serial I/O and `std::string` parsing, preserving the existing Pioneer command behavior and automatic-capture state machine.
- Implement minimal TOML support for the CLI’s own config keys: strings, booleans, integers, and simple section headers.
- Generate default output names as `RF-Sample_<timestamp>` or `TestData_<timestamp>`, with extension inferred from capture format.
- Write JSON sidecar metadata only when `--json <file>` or `capture.json` is supplied.
- Handle SIGINT/SIGTERM by stopping capture, unlocking key-lock if set, and stopping/pausing the player according to the automatic-capture flow.

## Test Plan
- Build `tools/cli-app` on Linux without Qt installed or discoverable.
- Unit-test:
  - CLI parsing and flag-over-config precedence.
  - minimal TOML parsing.
  - generated filename rules.
  - serial response parsing for model, state, disc type, frame/timecode, and lead-in/out markers.
  - automatic-capture state transitions using fake USB/player adapters.
- Manual hardware tests:
  - `list-devices` detects the USB capture device.
  - `capture` starts, reports progress, stops cleanly, and writes the expected file format.
  - `auto-capture` handles whole-disc, lead-in, and partial modes for CAV/CLV.
  - serial auto-detect works across supported baud rates.
  - Ctrl+C during capture leaves USB/player in a clean state.

## Assumptions
- `tools/dddconv` and `tools/dddutil` are unrelated and should not influence this design.
- Linux-first means v1 uses LibUSB and POSIX serial only; Windows support is deferred.
- `dddcli` must not include or link Qt libraries.
- Manual capture is included because automatic capture depends on the same capture engine.
- JSON sidecar metadata is opt-in and intentionally simpler than GUI metadata unless additional fields are requested.
- The existing GUI should remain buildable unless small shared USB-code extraction is required.
