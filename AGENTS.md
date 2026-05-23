# AGENTS.md

Guidance for agents working in this repository.

## Project Shape

`dddcli` is a C++20 command-line port of Domesday Duplicator capture/player workflows. It is intentionally small and mostly lives in:

- `src/main.cpp`: command dispatch, signal handling, USB capture loops, player actions, and auto-capture orchestration.
- `src/CliConfig.*`: option parsing, default config path, minimal TOML-style config loading, output naming, capture format conversion, and usage text.
- `src/PlayerSerial.*`: Pioneer-compatible serial player control using POSIX `termios`, `poll`, and raw command/response strings.
- `src/CaptureMetadata.*`: JSON sidecar metadata generation for captures.
- `src/ConsoleLogger.*`: adapter from the GUI USB logger interface to stderr.
- `tests/CliConfigTests.cpp`: assertion-based tests for config, parsing, and output format helpers.
- `orig/`: reference copy of the original GUI code. Use it to understand behavior that was ported from Qt dialogs/controllers, especially USB capture, configuration, naming, player communication, and automatic capture logic.

The CLI links against USB capture code from the GUI project. `CMakeLists.txt` currently expects a sibling checkout at `../DomesdayDuplicator` and uses the GUI USB classes (`UsbDeviceBase`, `UsbDeviceLibUsb`, `ILogger`). The `orig/` directory also contains reference copies of those classes, but the build file is the source of truth for what is compiled.

## Commands and Behavior

Supported commands are:

- `dddcli list-devices`
- `dddcli capture`
- `dddcli auto-capture`
- `dddcli player status|play|pause|stop|still|read-user-codes`

Config defaults come from `$XDG_CONFIG_HOME/domesday-duplicator/dddcli.toml`, then `$HOME/.config/domesday-duplicator/dddcli.toml`, then `dddcli.toml`. CLI flags override config values because `main()` loads config into a base `CliOptions` and reparses argv over it.

The config loader is intentionally minimal: sections plus `key = value`, comments with `#`, and simple string/number/bool parsing. Do not assume full TOML behavior unless you add tests for it.

## Build and Test

Use out-of-tree CMake builds:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If configuration fails, first verify libusb development files and the sibling Domesday Duplicator source/module paths. Tests currently exercise CLI/config code and should not require connected USB or serial hardware.

## Hardware-Sensitive Areas

Capture and auto-capture paths talk to real hardware:

- USB initialization uses the default VID/PID `0x1D50:0x603B` unless overridden.
- `capture` and `auto-capture` create output directories/files and may write large `.lds`, `.raw`, or `.cds` captures.
- `auto-capture` can spin up, stop, pause, seek, key-lock, and otherwise control a LaserDisc player through the serial port.
- `SIGINT`/`SIGTERM` set `stopRequested`; capture cleanup should continue to stop USB transfer and release key-lock where applicable.

When changing these flows, preserve the cleanup behavior in `runAutoCapture()` and the success/failure interpretation in `finishCapture()`. Be especially careful with player state transitions and key-lock release.

## Coding Conventions

- Keep the current simple C++ style: small helper functions, standard library facilities, and no new framework dependencies without a strong reason.
- Keep user-facing CLI text in `printUsage()` synchronized with parser changes.
- Add or update parser/config tests in `tests/CliConfigTests.cpp` when adding flags, config keys, formats, or default behaviors.
- Prefer explicit error messages to silent fallback for invalid user input.
- Keep generated JSON stable and escaped through the existing helpers in `CaptureMetadata.cpp`.
- Preserve stderr/stdout separation: status/progress/errors go to stderr; command results such as device paths and player status go to stdout.

## Useful Implementation Notes

- Output filenames default to `RF-Sample_YYYY-MM-DD_HH-MM-SS` or `TestData_...` with an extension based on capture format.
- `--output` without an extension gains the selected capture format extension.
- `--quiet` suppresses non-error logging and progress, but completion/error reporting is still important.
- Serial speed `auto` probes `9600`, `4800`, `2400`, then `1200`.
- CAV auto-capture uses frame addresses; CLV auto-capture uses time-code addresses.
- `AutoCaptureModeCli::Partial` requires `--end-address`.

## Git/Workspace Notes

Treat `orig/` as reference material unless the user explicitly asks to update the porting baseline. Prefer implementing CLI changes under `src/`, `tests/`, and build files. This repository may also sit next to the original GUI checkout; treat sibling files as dependencies, not owned source, unless the user explicitly asks to modify them. Do not revert unrelated worktree changes.
