# Configuration

`dddcli` accepts options from a small TOML-style config file and from command-line flags. Command-line flags override config values.

The config parser supports section headers, `key = value`, comments marked with `#`, quoted or unquoted scalar values, and simple booleans. It is intentionally not a full TOML implementation: arrays are not supported, `#` starts a comment outside quoted text, and unknown keys fail startup.

Values are applied before command-line parsing, so command-line flags always win over matching config keys.

## Config File Location

The default config path is selected from the environment:

1. If `$XDG_CONFIG_HOME` is set, `$XDG_CONFIG_HOME/domesday-duplicator/dddcli.toml`
2. Otherwise, if `$HOME` is set, `$HOME/.config/domesday-duplicator/dddcli.toml`
3. Otherwise, `dddcli.toml` in the current working directory

Use `--config <file>` after the command to choose a different file for one run, such as `dddcli capture --config ./dddcli.example.toml`.

Only the selected default path is checked; these locations are not searched as a fallback chain. Missing config files are allowed. Syntax errors fail startup.

## Example

An example config is available at [`dddcli.example.toml`](../dddcli.example.toml). It is safe to copy as a starting point because its active values match the built-in defaults or safe no-op settings; hardware paths, fixed capture durations, and partial auto-capture ranges are left commented until you opt into them.

To install it at the normal per-user location:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/domesday-duplicator"
cp dddcli.example.toml "${XDG_CONFIG_HOME:-$HOME/.config}/domesday-duplicator/dddcli.toml"
```

To try it without installing it:

```sh
dddcli capture --config ./dddcli.example.toml --duration 10
dddcli player status --config ./dddcli.example.toml --serial-device /dev/ttyUSB0
```

```toml
[usb]
vid = "0x1D50"
pid = "0x603B"
preferred_device = ""
use_small_usb_transfers = true
use_small_usb_transfer_queue = false
disk_buffer_queue_size = "256MiB"

[capture]
output_dir = "."
format = "lds"
json = ""
test_mode = false
# Uncomment to stop manual captures automatically instead of running until
# interrupted.
# duration_seconds = 10

[player]
# Uncomment and adjust before using player or auto-capture commands.
# serial_device = "/dev/ttyUSB0"
serial_speed = "auto"
profile = "auto"

[auto_capture]
mode = "whole-disc"
# Omit disc_type to let auto-capture detect the loaded disc type. Uncomment to
# require the loaded disc to match.
# disc_type = "clv"
# For mode = "partial", set both addresses. CLV addresses may be seconds,
# HMMSS, or HMMSSFF. CAV addresses are frames.
# start_address = "60"
# end_address = "90"
key_lock = false
on_screen_display = true
```

## CLI Options

Global options:

- `--config <file>`: config file path.
- `--debug`: enable debug logging from the USB backend.
- `--quiet`: suppress non-error status/progress output.

These options are global in effect, but they are still passed after the command name, and after the player action for `dddcli player`.

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

If `--output` is omitted, the generated filename is placed under `--output-dir`. If `--output` is present without an extension, the selected capture format extension is appended.

Player options:

- `--serial-device <path>`: serial device, such as `/dev/ttyUSB0`.
- `--serial-speed auto|9600|4800|2400|1200`: serial speed. `auto` probes common Pioneer speeds.
- `--player-profile auto|generic-level3|pioneer-ld-v4300d|pioneer-ld-v2200`: command profile.

Auto-capture options:

- `--disc-type cav|clv`: optional override for the player-detected disc type. If supplied, auto-capture fails when the loaded disc does not match.
- `--mode whole-disc|lead-in|partial`: capture range mode.
- `--start-address <n>` and `--end-address <n>`: frame address for CAV, normalized seconds or compact timecode for CLV. Partial auto-capture requires the normalized end address to be greater than the start address.
- `--key-lock`: key-lock the player during capture and release it during cleanup. If the player cannot enable key lock, auto-capture aborts before capture starts.
- `--on-screen-display` / `--no-on-screen-display`: turn the player's on-screen display on or off during auto-capture setup. The default is on.

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
- `[auto_capture] on_screen_display`

There is currently no config key for `--output`, `--debug`, `--quiet`, or the `--large-*` CLI convenience toggles. Set the matching boolean keys to `false` for large/default USB transfer behavior.

## Defaults And Aliases

Defaults:

- USB VID/PID: `0x1D50:0x603B`
- disk buffer queue size: `256MiB`
- output directory: `.`
- capture format: `lds`
- serial speed: `auto`
- player profile: `auto`
- auto-capture disc type: auto-detected from the player
- auto-capture mode: `whole-disc`
- auto-capture on-screen display: enabled
- USB transfers: small transfers enabled, reduced transfer queue disabled

The command line and config parser accept the canonical values shown above plus a few compatibility aliases:

- capture formats: `ten-bit-packed` or `10bit` for `lds`, `sixteen-bit-signed` or `16bit` for `raw`, and `ten-bit-cd-packed` or `cd` for `cds`
- player profiles: `generic`, `generic-level-3`, `ld-v4300d`, `ldv4300d`, `ld-v2200`, and `ldv2200`
- auto-capture modes: `whole` for `whole-disc` and `leadin` for `lead-in`
- booleans in config: `true/false`, `1/0`, `yes/no`, and `on/off`
- sizes: plain bytes, `mb`, or `mib`

## CLV Addresses

Internally, CLV addresses are player-reported timecodes normalized to seconds.

For `--start-address`, `--end-address`, and config addresses:

- values below 5 digits are seconds, such as `754`
- 5 digits are parsed as `HMMSS`, such as `01234`
- 7 digits are parsed as `HMMSSFF`, such as `0123400`, with frame digits ignored

For example, `754`, `01234`, and `0123400` all refer to 12 minutes and 34 seconds.

CLV addresses are absolute displayed timecodes from the player, not offsets relative to the first playable timecode on a disc. Whole-disc auto-capture starts from spin-down/lead-in and can handle CLV discs that do not begin at `0:00:00`; for `partial` or `lead-in` ranges, supply the actual displayed timecodes you want to target.

When reading player responses, the CLI also accepts compact `HMM` from minute-only CLV timecodes and normalizes it to the first second of that minute. If a detected CLV disc end lands exactly on a minute boundary, auto-capture uses a longer end post-roll so whole-disc captures do not lose the rest of the final minute.
