# Hardware Test Checklist

Use this checklist when real Domesday Duplicator USB hardware and a Pioneer serial-controllable LaserDisc player are connected. Replace `/dev/ttyUSB0` with the actual serial device.

These tests can move the player, spin the disc, seek, key-lock the panel, and write capture files. Start with a non-valuable test disc and keep output paths pointed at scratch storage.

## 1. Preflight

- Build and run the software-only tests:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- Confirm the DdD USB device is visible:

```sh
./build/dddcli list-devices
```

- Confirm the serial device exists and the user can access it:

```sh
ls -l /dev/ttyUSB0
```

## 2. Player Detection And Profile Selection

Run each command with the target player connected.

```sh
./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed auto
```

Record:

- detected model
- detected serial speed
- reported `playerProfile`
- `state`
- `discType`
- `discStatus`

Expected profile mappings:

- LD-V4300D model code `15`: `pioneer-ld-v4300d`
- LD-V2200 model code `07`: `pioneer-ld-v2200`
- unknown compatible player: `generic-level3`

Also verify manual override:

```sh
./build/dddcli player status --serial-device /dev/ttyUSB0 --player-profile pioneer-ld-v2200
./build/dddcli player status --serial-device /dev/ttyUSB0 --player-profile pioneer-ld-v4300d
./build/dddcli player status --serial-device /dev/ttyUSB0 --player-profile generic-level3
```

## 3. Raw Serial Evidence

Capture raw responses for commands used by the profiles:

```sh
./build/dddcli player raw-command '?X' --serial-device /dev/ttyUSB0
./build/dddcli player raw-command '?P' --serial-device /dev/ttyUSB0
./build/dddcli player raw-command '?D' --serial-device /dev/ttyUSB0
./build/dddcli player raw-command '?F' --serial-device /dev/ttyUSB0
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0
./build/dddcli player raw-command '?U' --serial-device /dev/ttyUSB0
./build/dddcli player raw-command '$Y' --serial-device /dev/ttyUSB0
```

Record the escaped output exactly. Especially note whether CLV `?T` returns 5 digits (`HMMSS`) or 7 digits (`HMMSSFF`) on each player.

## 4. Basic Player Controls

With a disc loaded:

```sh
./build/dddcli player play --serial-device /dev/ttyUSB0
./build/dddcli player pause --serial-device /dev/ttyUSB0
./build/dddcli player still --serial-device /dev/ttyUSB0
./build/dddcli player stop --serial-device /dev/ttyUSB0
```

After each command, run:

```sh
./build/dddcli player status --serial-device /dev/ttyUSB0
```

Expected: command succeeds, status reflects the new state, and the player does not drop serial communication.

## 5. CLV Timecode Behavior

Use a CLV disc.

```sh
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --player-profile pioneer-ld-v2200
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --player-profile pioneer-ld-v4300d
```

Expected:

- LD-V2200: 5-digit `HMMSS` is acceptable and should normalize to elapsed seconds internally.
- LD-V4300D/generic: 5-digit `HMMSS` or 7-digit `HMMSSFF` is acceptable.
- Lead-in/lead-out markers `<` and `>` should be preserved in raw output and tolerated by parsing.

Then perform a short partial CLV auto-capture with safe bounds:

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type clv \
  --mode partial \
  --start-address 60 \
  --end-address 90 \
  --output /tmp/dddcli-clv-test.lds \
  --json /tmp/dddcli-clv-test.json
```

Expected:

- player seeks near 1 minute
- capture continues through second 90 and stops after the player reports second 91, after the CLV 1.5 second post-roll fallback, or cleanly if the player stops/pauses/still-frames during post-roll
- JSON `minTimeCode` and `maxTimeCode` are normalized seconds, not raw compact timecode

Repeat compact input forms:

```sh
./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --disc-type clv --mode partial --start-address 00100 --end-address 00130 --output /tmp/dddcli-clv-hmmss.lds --json /tmp/dddcli-clv-hmmss.json
./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --disc-type clv --mode partial --start-address 0010000 --end-address 0013000 --output /tmp/dddcli-clv-hmmssff.lds --json /tmp/dddcli-clv-hmmssff.json
```

Expected: both runs behave like 60-to-90 second captures.

## 6. CAV Frame Behavior

Use a CAV disc.

```sh
./build/dddcli player raw-command '?F' --serial-device /dev/ttyUSB0
```

Expected: frame response remains frame-number based and is not interpreted as CLV seconds.

Run a short partial CAV auto-capture:

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type cav \
  --mode partial \
  --start-address 1000 \
  --end-address 1300 \
  --output /tmp/dddcli-cav-test.lds \
  --json /tmp/dddcli-cav-test.json
```

Expected:

- player seeks to frame 1000
- capture stops around frame 1300
- JSON uses `minFrameNumber` and `maxFrameNumber`

## 7. Key Lock And Cleanup

Run a short capture with key lock:

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type cav \
  --mode partial \
  --start-address 1000 \
  --end-address 1100 \
  --key-lock \
  --output /tmp/dddcli-keylock-test.lds
```

Expected:

- key lock engages during capture
- key lock is released when capture completes

Repeat and interrupt with `Ctrl-C`.

Expected:

- USB capture stops cleanly
- key lock is released
- player ends in the documented post-capture state

## 8. Manual Capture Smoke Test

This verifies USB capture without player automation:

```sh
./build/dddcli capture \
  --duration 5 \
  --output /tmp/dddcli-manual-test.lds \
  --json /tmp/dddcli-manual-test.json
```

Expected:

- capture file is created
- JSON sidecar is created
- transfer result is `success` or an understood hardware error

## 9. Results To Keep

For each player/profile combination, keep:

- exact CLI command
- stdout/stderr
- raw-command responses
- player model/version/profile
- disc type used
- whether the disc was CAV or CLV
- JSON sidecar from successful captures
- any player behavior that differs from the CLI output
