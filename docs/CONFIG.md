# Configuration

`dddcli` accepts options from a small TOML-style config file and from command-line flags. Command-line flags override config values.

The config parser supports section headers, `key = value`, comments beginning with `#`, quoted or unquoted scalar values, and simple booleans. It is intentionally not a full TOML implementation.

## Config File Location

The default config path is chosen in this order:

1. `$XDG_CONFIG_HOME/domesday-duplicator/dddcli.toml`
2. `$HOME/.config/domesday-duplicator/dddcli.toml`
3. `dddcli.toml` in the current working directory

Use `--config <file>` to choose a different file.

Missing config files are allowed. Syntax errors fail startup.

## Example

```toml
[usb]
vid = "0x1D50"
pid = "0x603B"
use_small_usb_transfers = true
use_small_usb_transfer_queue = false
disk_buffer_queue_size = "256MiB"

[capture]
output_dir = "/capture"
format = "lds"
json = "/capture/latest.json"
test_mode = false

[player]
serial_device = "/dev/ttyUSB0"
serial_speed = "auto"
profile = "auto"

[auto_capture]
disc_type = "clv"
mode = "partial"
start_address = "60"
end_address = "90"
key_lock = false
```

## CLI Options

Global options:

- `--config <file>`: config file path.
- `--debug`: enable debug logging from the USB backend.
- `--quiet`: suppress non-error status/progress output.

USB options:

- `--vid <id>` and `--pid <id>`: USB IDs, decimal or `0x` prefixed.
- `--usb-device <path>`: preferred USB device path.
- `--disk-buffer-queue-size <size>`: disk buffer queue size. Plain numbers are bytes; `mb` and `mib` suffixes are accepted.
- `--small-usb-transfer-queue` / `--large-usb-transfer-queue`: choose reduced or configured USB queue size.
- `--small-usb-transfers` / `--large-usb-transfers`: choose USB transfer sizing.

Capture options:

- `--output <file>`: capture output path.
- `--json <file>`: JSON metadata sidecar path.
- `--output-dir <dir>`: directory for generated output names.
- `--format lds|raw|cds`: capture format.
- `--test-mode`: capture the DdD test pattern.
- `--duration <seconds>`: stop manual capture after this duration.

Player options:

- `--serial-device <path>`: serial device, such as `/dev/ttyUSB0`.
- `--serial-speed auto|9600|4800|2400|1200`: serial speed. `auto` probes common Pioneer speeds.
- `--player-profile auto|generic-level3|pioneer-ld-v4300d|pioneer-ld-v2200`: command profile.

Auto-capture options:

- `--disc-type cav|clv`: required for auto-capture.
- `--mode whole-disc|lead-in|partial`: capture range mode.
- `--start-address <n>` and `--end-address <n>`: frame address for CAV, normalized seconds or compact timecode for CLV.
- `--key-lock`: key-lock the player during capture and release it during cleanup.

## Config Keys

The supported config keys are:

- `[usb] vid`
- `[usb] pid`
- `[usb] preferred_device`
- `[usb] disk_buffer_queue_size`
- `[usb] use_small_usb_transfer_queue`
- `[usb] use_small_usb_transfers`
- `[capture] output_dir`
- `[capture] json`
- `[capture] format`
- `[capture] test_mode`
- `[capture] duration_seconds`
- `[player] serial_device`
- `[player] serial_speed`
- `[player] profile`
- `[auto_capture] disc_type`
- `[auto_capture] mode`
- `[auto_capture] start_address`
- `[auto_capture] end_address`
- `[auto_capture] key_lock`

There is currently no config key for `--output`, `--debug`, or `--quiet`.

## CLV Addresses

Internally, CLV addresses are normalized elapsed seconds.

For `--start-address`, `--end-address`, and config addresses:

- values below 5 digits are seconds, such as `754`
- 5 digits are parsed as `HMMSS`, such as `01234`
- 7 digits are parsed as `HMMSSFF`, such as `0123400`, with frame digits ignored

For example, `754`, `01234`, and `0123400` all refer to 12 minutes and 34 seconds.
