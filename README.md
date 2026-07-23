# dddcli

`dddcli` is a C++20 command-line tool for Domesday Duplicator capture workflows. It can list connected DdD USB devices, capture RF samples, control Pioneer-compatible LaserDisc players over serial, and run automated captures that coordinate the USB capture with player transport state.

This is hardware-facing software. Capture commands can write large files, and player commands can spin discs, seek, stop, pause, and key-lock the front panel.

## Build

`dddcli` is built with CMake. The active code is self-contained under `src/`. The build requires the libusb development package to be discoverable through `pkg-config` as `libusb-1.0`.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Software tests do not require USB or serial hardware.

## Linux Host Setup

For reliable non-root capture on Linux, the Domesday Duplicator USB device should have a late udev rule such as `/etc/udev/rules.d/99-domesday.rules`:

```text
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="603b", MODE="0666"
```

The rule must use `ATTR{...}` and run after the default USB rules so the final `/dev/bus/usb/...` node is writable by the capture user. The default capture queue also expects `usbcore.usbfs_memory_mb=512`; if that is set through `/etc/modprobe.d/usbcore.conf`, the active initramfs may need to be rebuilt before the value survives reboot. See [Troubleshooting](docs/TROUBLESHOOTING.md) for the full checks.

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

Auto-capture turns the player's on-screen display on by default. Use `--no-on-screen-display` when the player OSD should be disabled during capture.

Interactive captures show one updating progress line on stderr. When stderr is redirected or piped through `tee`, captures emit newline progress about every 10 seconds; auto-capture progress includes `timecode=H:MM:SS` for CLV or `frame=<n>` for CAV once the player address is known. Successful auto-captures stop the player during cleanup.

## Configuration

An example config is provided at [dddcli.example.toml](dddcli.example.toml). Copy it to `${XDG_CONFIG_HOME:-$HOME/.config}/domesday-duplicator/dddcli.toml` for normal use, or pass it after the command for a one-off run:

```sh
./build/dddcli capture --config ./dddcli.example.toml --duration 10
```

Command-line flags override config values. See [Configuration](docs/CONFIG.md) for the supported keys and default path rules.

## Commands

Flags may appear before, between, or after command words, so stable settings can stay in place while changing the command action.

- `dddcli list-devices`: print visible Domesday Duplicator USB device paths.
- `dddcli capture`: start a manual USB capture.
- `dddcli auto-capture`: coordinate USB capture with serial player control.
- `dddcli player` or `dddcli player status`: print model, active profile, player state, disc type, and disc status.
- `dddcli player play|pause|stop|still`: send basic transport commands.
- `dddcli player read-user-codes`: query standard and Pioneer user code fields.
- `dddcli player raw-command <command>`: send a raw serial command and print an escaped response.

## Output

Supported capture formats are:

- `lds`: 10-bit packed RF samples, the default.
- `raw`: 16-bit signed samples.
- `cds`: 10-bit packed CD capture format.

If `--output` has no extension, the selected format extension is added. If no output path is provided, the tool creates a timestamped file under `--output-dir`, which defaults to the current directory. Generated filenames use `RF-Sample_YYYY-MM-DD_HH-MM-SS` or `TestData_YYYY-MM-DD_HH-MM-SS` when `--test-mode` is enabled.

Use `--json <file>` to write a metadata sidecar. The sidecar contains `captureInfo` for every capture, including the capture path, format, transfer result, duration, transfer and disk-buffer counts, file size, sample count, sample min/max, clipping counts, sequence-marker presence, and UTC creation timestamp. Auto-captures also populate `serialInfo` with player and disc fields; CAV metadata records `minFrameNumber` and `maxFrameNumber`, while CLV metadata records `minTimeCode` and `maxTimeCode` as player-reported timecodes normalized to seconds.

## Documentation

- [Configuration](docs/CONFIG.md)
- [Player Control](docs/PLAYER_CONTROL.md)
- [Auto Capture](docs/AUTO_CAPTURE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [USB Capture Performance Testing](tests/manual/USB_CAPTURE_PERFORMANCE_TESTING.md)
- [Hardware Test Checklist](tests/manual/HARDWARE_TESTS.md)

## License and Attribution

This codebase is derived from the original [DomesdayDuplicator](https://github.com/simoninns/DomesdayDuplicator) project by [Simon Inns](https://github.com/simoninns). The upstream project is licensed under the GNU General Public License v3.0; this derived command-line port preserves that license for the code carried forward from DomesdayDuplicator. It is distributed without warranty. See [LICENSE](LICENSE) for the full license text and [NOTICE](NOTICE) for attribution details.

## AI Assistance

Development of this command-line port was substantially assisted by OpenAI Codex and ChatGPT 5.5, working from Ed Powell's direction, testing, hardware context, and review.

For contributor and agent guidance, see [AGENTS.md](AGENTS.md).
