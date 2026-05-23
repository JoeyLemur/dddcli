# dddcli

`dddcli` is a C++20 command-line tool for Domesday Duplicator capture workflows. It can list connected DdD USB devices, capture RF samples, control Pioneer-compatible LaserDisc players over serial, and run automated captures that coordinate the USB capture with player transport state.

This is hardware-facing software. Capture commands can write large files, and player commands can spin discs, seek, stop, pause, and key-lock the front panel.

## Build

`dddcli` is built with CMake. The build expects the Domesday Duplicator GUI source checkout as a sibling directory because the CLI links against the shared USB capture classes from that project. CMake looks for the shared capture sources under `../DomesdayDuplicator/gui-app/tools/DomesdayDuplicator`.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Software tests do not require USB or serial hardware.

## Basic Usage

List connected Domesday Duplicator USB devices:

```sh
./build/dddcli list-devices
```

Capture manually for a fixed duration:

```sh
./build/dddcli capture \
  --duration 10 \
  --output /tmp/sample.lds \
  --json /tmp/sample.json
```

Check a connected Pioneer-compatible LaserDisc player:

```sh
./build/dddcli player status \
  --serial-device /dev/ttyUSB0 \
  --serial-speed auto
```

Run a partial CAV auto-capture:

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type cav \
  --mode partial \
  --start-address 1000 \
  --end-address 1300 \
  --output /tmp/cav-test.lds \
  --json /tmp/cav-test.json
```

Run a partial CLV auto-capture:

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type clv \
  --mode partial \
  --start-address 60 \
  --end-address 90 \
  --output /tmp/clv-test.lds \
  --json /tmp/clv-test.json
```

## Commands

- `dddcli list-devices`: print visible Domesday Duplicator USB device paths.
- `dddcli capture`: start a manual USB capture.
- `dddcli auto-capture`: coordinate USB capture with serial player control.
- `dddcli player status`: print model, active profile, player state, disc type, and disc status.
- `dddcli player play|pause|stop|still`: send basic transport commands.
- `dddcli player read-user-codes`: query standard and Pioneer user code fields.
- `dddcli player raw-command <command>`: send a raw serial command and print an escaped response.

## Output

Supported capture formats are:

- `lds`: 10-bit packed RF samples, the default.
- `raw`: 16-bit signed samples.
- `cds`: 10-bit packed CD capture format.

If `--output` has no extension, the selected format extension is added. If no output path is provided, the tool creates a timestamped file under `--output-dir`, which defaults to the current directory. Generated filenames use `RF-Sample_YYYY-MM-DD_HH-MM-SS` or `TestData_YYYY-MM-DD_HH-MM-SS` when `--test-mode` is enabled.

Use `--json <file>` to write a metadata sidecar. For auto-captures, CAV metadata records `minFrameNumber` and `maxFrameNumber`; CLV metadata records `minTimeCode` and `maxTimeCode` as normalized elapsed seconds.

## Documentation

- [Configuration](docs/CONFIG.md)
- [Player Control](docs/PLAYER_CONTROL.md)
- [Auto Capture](docs/AUTO_CAPTURE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Hardware Test Checklist](HARDWARE_TESTS.md)

For contributor and agent guidance, see [AGENTS.md](AGENTS.md).
