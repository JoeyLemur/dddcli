# Hardware Test Checklist

Use this checklist when real Domesday Duplicator USB hardware and a Pioneer serial-controllable LaserDisc player are connected. Replace `/dev/ttyUSB0` with the actual serial device.

These tests can move the player, spin the disc, seek, key-lock the panel, and write capture files. Start with a non-valuable test disc and keep output paths pointed at scratch storage.

Run serial/player commands one at a time. The player serial protocol is a single shared channel, and overlapping probes can corrupt or consume each other's responses.

Commands below use explicit output paths. If `--output` is omitted, captures are written under `--output-dir` or the current directory using timestamped `RF-Sample_...` or `TestData_...` filenames.

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

If a device path is reported, confirm it negotiated as USB 3 SuperSpeed. Replace `4-1` with the device path suffix reported by `list-devices`.

```sh
cat /sys/bus/usb/devices/4-1/speed
cat /sys/bus/usb/devices/4-1/version
```

Expected: USB 3 SuperSpeed reports speed `5000` and version `3.00` or newer.

### Linux Capture Host Tuning

Linux capture hosts should provide enough USBFS and locked-memory headroom for the default capture queues:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
ulimit -l
```

Expected:

- `usbfs_memory_mb` is at least `512`
- `ulimit -l` is at least `524288` KiB, or `unlimited`

To set the USBFS memory pool temporarily until reboot:

```sh
echo 512 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
```

To make the USBFS setting persistent, add a modprobe setting:

```sh
echo 'options usbcore usbfs_memory_mb=512' | sudo tee /etc/modprobe.d/usbcore.conf
```

To raise the locked-memory limit for a PAM-login shell, create a limits file such as `/etc/security/limits.d/domesday.conf`:

```text
captureuser soft memlock 524288
captureuser hard memlock 524288
@domesday soft memlock 524288
@domesday hard memlock 524288
```

Use a bare name such as `captureuser` for a user-specific limit, or an `@` prefix such as `@domesday` for a group limit. Existing sessions keep their old limits, so start a fresh login session before re-checking. For a systemd service, set `LimitMEMLOCK=512M` in the service unit.

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

With no disc loaded, a working player may report `state=stop`, `discType=unknown`, and a model-specific no-disc status such as `0XXXX`.

`dddcli player` without an explicit action should produce the same status output as `dddcli player status`.

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

With no disc loaded, address/user-code requests may return an error response such as `E04\r`; record that as the no-disc baseline rather than treating it as a CLI failure if the command exits successfully.

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
- `captureInfo.transferResultString` is `success`, `forced abort`, or an understood hardware error if the test is interrupted or hardware fails

If the default queue fails before capture starts with `USB memory limit` or `LIBUSB_ERROR_NO_MEM`, re-check the Linux capture host tuning above. If the host cannot be tuned, repeat with the smaller USB transfer queue:

```sh
./build/dddcli capture \
  --duration 5 \
  --small-usb-transfer-queue \
  --output /tmp/dddcli-manual-test-smallq.lds \
  --json /tmp/dddcli-manual-test-smallq.json
```

For USB 3 validation, record `captureInfo.durationInMilliseconds`, `captureInfo.fileSizeWrittenInBytes`, and the approximate sustained rate. A successful 5 second run should produce a non-empty capture at the expected DdD data rate while the kernel device speed remains `5000`.

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
