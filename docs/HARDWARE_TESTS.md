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
udevadm info --query=property --path=/sys/bus/usb/devices/4-1
```

Expected: USB 3 SuperSpeed reports speed `5000` and version `3.00` or newer.

On Linux, confirm the matching `/dev/bus/usb/...` node is writable by the capture user. Use `BUSNUM` and `DEVNUM` from `udevadm info`; for example, `BUSNUM=004` and `DEVNUM=002` correspond to `/dev/bus/usb/004/002`.

```sh
ls -l /dev/bus/usb/004/002
```

Expected: the node is writable by the capture user, for example `crw-rw-rw-` when using a late `/etc/udev/rules.d/99-domesday.rules` rule. If it is still `0664 root:root`, see `TROUBLESHOOTING.md`.

### Linux Capture Host Tuning

Linux capture hosts should provide enough USBFS and locked-memory headroom for the default capture queues:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
ulimit -l
ulimit -r
```

Expected:

- `usbfs_memory_mb` is at least `512`
- `ulimit -l` is at least `524288` KiB, or `unlimited`
- `ulimit -r` is high enough for realtime capture priority, such as `80` or higher

To set the USBFS memory pool temporarily until reboot:

```sh
echo 512 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
```

To make the USBFS setting persistent, add a modprobe setting:

```sh
echo 'options usbcore usbfs_memory_mb=512' | sudo tee /etc/modprobe.d/usbcore.conf
```

If `usbcore` is loaded from the initramfs during boot, rebuild the active initramfs after adding the modprobe setting. On Debian/Ubuntu-style systems:

```sh
sudo update-initramfs -u
```

Then reboot and re-check:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
```

If the value is still not `512`, add `usbcore.usbfs_memory_mb=512` to the kernel command line instead.

To raise the locked-memory limit for a PAM-login shell, create a limits file such as `/etc/security/limits.d/domesday.conf`:

```text
captureuser soft memlock 524288
captureuser hard memlock 524288
@domesday soft memlock 524288
@domesday hard memlock 524288
captureuser soft rtprio 80
captureuser hard rtprio 80
@domesday soft rtprio 80
@domesday hard rtprio 80
```

Use a bare name such as `captureuser` for a user-specific limit, or an `@` prefix such as `@domesday` for a group limit. Existing sessions keep their old limits, so start a fresh login session before re-checking. For a systemd service, set `LimitMEMLOCK=512M` and `LimitRTPRIO=80` in the service unit.

If capture prints `warning: SetCurrentThreadRealtimePriority: Unable to set thread priority`, the process did not receive realtime scheduling permission. Capture can continue, but configuring `rtprio` removes that warning and gives the capture threads better scheduler priority.

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

Record the escaped output exactly. Especially note whether CLV `?T` returns 3 digits (`HMM`), 5 digits (`HMMSS`), or 7 digits (`HMMSSFF`) on each player.

With no disc loaded, address/user-code requests may return an error response such as `E04\r`; record that as the no-disc baseline rather than treating it as a CLI failure if the command exits successfully. On some players, `?U` may be unsupported, so an `E04\r` response should not be treated as proof that no Pioneer User's Code is encoded unless the model's command set confirms `?U`.

## 4. Basic Player Controls

With a disc loaded:

```sh
./build/dddcli player play --serial-device /dev/ttyUSB0
./build/dddcli player pause --serial-device /dev/ttyUSB0
./build/dddcli player stop --serial-device /dev/ttyUSB0
```

After each command, run:

```sh
./build/dddcli player status --serial-device /dev/ttyUSB0
```

Expected: command succeeds, status reflects the new state, and the player does not drop serial communication.

Test `still` separately with a CAV disc:

```sh
./build/dddcli player play --serial-device /dev/ttyUSB0
./build/dddcli player still --serial-device /dev/ttyUSB0
./build/dddcli player status --serial-device /dev/ttyUSB0
./build/dddcli player stop --serial-device /dev/ttyUSB0
```

Expected: CAV `still` succeeds and status reports `state=still-frame`. On CLV discs, some players such as the LD-V2200 reject `ST` with `E04`; record that model-specific response rather than treating it as a serial transport failure.

## 5. CLV Timecode Behavior

Use a CLV disc.

```sh
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --player-profile pioneer-ld-v2200
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --player-profile pioneer-ld-v4300d
```

Expected:

- Minute-only CLV discs: 3-digit `HMM` is acceptable and should normalize to the first second of that displayed minute.
- LD-V2200: 5-digit `HMMSS` is acceptable and should normalize to seconds internally.
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
- JSON `maxTimeCode` reflects addresses seen during capture; partial mode does not perform a disc-end probe

Repeat compact input forms:

```sh
./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --disc-type clv --mode partial --start-address 00100 --end-address 00130 --output /tmp/dddcli-clv-hmmss.lds --json /tmp/dddcli-clv-hmmss.json
./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --disc-type clv --mode partial --start-address 0010000 --end-address 0013000 --output /tmp/dddcli-clv-hmmssff.lds --json /tmp/dddcli-clv-hmmssff.json
```

Expected: both runs behave like 60-to-90 second captures.

### Normal CLV Disc End Handling

With a CLV disc that reports normal second-granular timecodes, run a whole-disc capture. Use the shortest suitable test disc if capture time or storage is limited, but keep `--mode whole-disc` so the disc-end probe and detected-end stop path are exercised.

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type clv \
  --mode whole-disc \
  --output /tmp/dddcli-clv-whole-disc.lds \
  --json /tmp/dddcli-clv-whole-disc.json
```

If stderr is piped through `tee` for logging, progress is emitted as newline status about every 10 seconds. CLV auto-capture progress includes `timecode=H:MM:SS` after the first address is read.

Expected:

- player probes the CLV end, spins down, then captures from lead-in/playback start
- capture reaches the detected end timecode and does not stop before the final reported second
- capture stops after the player reports the next second, after the CLV 1.5 second post-roll fallback, or cleanly if the player stops/pauses/still-frames during post-roll
- if the player restarts from the beginning at end-of-disc, capture stops when the near-end timecode wraps back to an earlier timecode
- a near-end partial CLV capture with `--end-address` close to the physical end stops cleanly if the player wraps
- final player cleanup stops the CLV disc
- JSON `minTimeCode` and `maxTimeCode` reflect addresses seen during capture and do not include the earlier disc-end probe

### Minute-Only CLV Disc End Handling

When a CLV disc with hour/minute-only timecodes is available, run this test in addition to the short partial capture above. Use scratch storage with enough free space for a full CLV capture.

First confirm the player reports minute-only timecodes:

```sh
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0
```

Expected: the raw response uses 3-digit `HMM` timecodes, optionally with `<` or `>` lead-in/lead-out markers. Record several responses during playback near the end of the disc if possible.

Then run a whole-disc CLV auto-capture:

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type clv \
  --mode whole-disc \
  --output /tmp/dddcli-clv-minute-only.lds \
  --json /tmp/dddcli-clv-minute-only.json
```

Expected:

- stderr reports `Detected minute-aligned CLV disc end; using 61 second end post-roll`
- capture does not stop immediately when the final reported minute is first reached
- capture continues through the final minute, or ends cleanly earlier only if the player reports `Stop`, `Pause`, or `StillFrame`
- JSON `maxTimeCode` is on a minute boundary for the minute-only reported address
- JSON `maxTimeCode` reflects addresses seen during capture and does not include the earlier disc-end probe

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
- JSON `maxFrameNumber` reflects frames seen during capture and does not include the earlier disc-end probe

### CAV Disc End Handling

Run a whole-disc CAV auto-capture. Use the shortest suitable test disc if capture time or storage is limited, but keep `--mode whole-disc` so the frame `60000` disc-end probe and detected-end stop path are exercised.

```sh
./build/dddcli auto-capture \
  --serial-device /dev/ttyUSB0 \
  --disc-type cav \
  --mode whole-disc \
  --output /tmp/dddcli-cav-whole-disc.lds \
  --json /tmp/dddcli-cav-whole-disc.json
```

Expected:

- player probes the CAV end, spins down, then captures from lead-in/playback start
- capture reaches the detected final frame and does not stop before that frame
- capture stops when the reported frame is at or beyond the detected end frame
- final player cleanup stops the CAV disc
- JSON `minFrameNumber` and `maxFrameNumber` reflect frames seen during capture and do not include the earlier frame `60000` disc-end probe

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

Repeat once more and interrupt during setup, before the `Capturing to ...` message appears if possible.

Expected:

- USB capture is not started
- playback is not started by the CLI after the interrupt
- key lock is released if it had already been enabled

If a CAV player enters still-frame during capture and cannot be resumed by the CLI, expected behavior is:

- USB capture stops cleanly
- the command exits non-zero
- the player is left in still-frame for inspection
- JSON metadata is written if `--json` was requested and USB capture had already started

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
