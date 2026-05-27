# Troubleshooting

## Build Fails

Run:

```sh
cmake -S . -B build
```

If configuration fails, check:

- libusb development files are installed
- the Domesday Duplicator GUI source checkout exists as the expected sibling directory
- the GUI CMake modules are available from that checkout

Then rebuild:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

## No USB Devices Found

Run:

```sh
./build/dddcli list-devices
```

If no devices are found:

- confirm the DdD hardware is connected and powered
- check USB permissions for the current user
- try a different cable or port
- verify VID/PID overrides if using `--vid` or `--pid`

The defaults are VID `0x1D50` and PID `0x603B`. When no matching devices are visible, `list-devices` prints `No Domesday Duplicator USB devices found` and exits non-zero.

## Player Does Not Connect

Start with:

```sh
./build/dddcli player status \
  --serial-device /dev/ttyUSB0 \
  --serial-speed auto
```

If it fails:

- confirm the serial device path
- confirm the current user can access the serial device
- try an explicit speed: `9600`, `4800`, `2400`, or `1200`
- check cabling and null-modem requirements for the player/interface
- try a raw model query:

```sh
./build/dddcli player raw-command '?X' --serial-device /dev/ttyUSB0
```

## Wrong Player Profile

Check:

```sh
./build/dddcli player status --serial-device /dev/ttyUSB0
```

The output includes `playerProfile`. If `auto` chooses the wrong profile or the player has unknown behavior, force one:

```sh
./build/dddcli player status \
  --serial-device /dev/ttyUSB0 \
  --player-profile pioneer-ld-v2200
```

Use `player raw-command` to collect exact responses before changing command behavior.

## CLV Timecode Looks Wrong

CLV addresses are player-reported timecodes normalized to seconds internally. The CLI accepts:

- `754`
- `01234`
- `0123400`

All three mean 12 minutes and 34 seconds.

These values are absolute displayed timecodes from the player. Whole-disc CLV auto-capture does not require the disc to begin at `0:00:00`, but manually supplied `--start-address` and `--end-address` values should match the disc's displayed timecodes rather than offsets from the first playable code.

Some older CLV discs only expose hour/minute precision. If whole-disc capture detects an end timecode on an exact minute boundary, the CLI waits up to 60 seconds after first seeing that end address so it does not drop the rest of the final minute. It still stops early if the player reaches a terminal state during that post-roll.

For raw hardware evidence, query:

```sh
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0
```

Record whether the response is 3-digit `HMM`, 5-digit `HMMSS`, 7-digit `HMMSSFF`, or has lead-in/lead-out markers.

## Auto-Capture Stops Late Or Early

CAV stops at the requested frame once the player reports that frame or later.

CLV intentionally captures past the first report of the requested end second. It stops after the player advances to the next second, after a 1.5 second post-roll timeout, or cleanly if the player stops/pauses/still-frames during that post-roll.

For CLV captures, a wrap from a near-end timecode back to an earlier timecode is treated as the end of the requested range. This prevents players that restart from the beginning at end-of-disc from continuing capture indefinitely, and also lets a near-end partial capture finish cleanly if it reaches the physical end.

If auto-capture fails with address-read errors:

- verify the selected `--disc-type`
- check `player status`
- collect raw `?F` for CAV or `?T` for CLV
- record the profile, serial speed, command output, and disc used

## Capture File Problems

Capture files can be large. If capture fails after starting:

- confirm the output filesystem has enough free space
- write to fast local storage when possible
- try the default `.lds` format first
- on Linux, use the host tuning below if startup reports `USB memory limit`, `LIBUSB_ERROR_NO_MEM`, or `mlock failed`
- use `--small-usb-transfer-queue` only when the host cannot be tuned
- if writes cannot keep up, keep `--small-usb-transfers` enabled and try faster local storage
- keep the JSON sidecar; it records the transfer result and sample statistics

If `--json` points to a path in a missing directory, the CLI creates that directory before writing the sidecar.

## Capture Progress In Logs

When stderr is attached to a terminal, capture progress is shown as a single updating line. When stderr is redirected or piped through a command such as `tee`, progress is written as newline-delimited status about every 10 seconds so long captures still leave useful log evidence. Auto-capture progress also includes the current player position as `timecode=H:MM:SS` for CLV or `frame=<n>` for CAV after the first address is read. `--quiet` suppresses both forms of progress output.

### Linux Capture Host Tuning

The default capture queue needs enough kernel USBFS memory for libusb transfers and enough locked-memory allowance for capture buffers. Use these capture-host defaults:

- `usbcore.usbfs_memory_mb=512`
- `memlock=524288` KiB, or `unlimited`
- `rtprio=80` or higher, so capture threads can request realtime priority

Check the active values:

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
ulimit -l
ulimit -r
```

Set USBFS memory until reboot:

```sh
echo 512 | sudo tee /sys/module/usbcore/parameters/usbfs_memory_mb
```

Make USBFS memory persistent with `/etc/modprobe.d/usbcore.conf`:

```text
options usbcore usbfs_memory_mb=512
```

Some systems need the initramfs rebuilt or `usbcore.usbfs_memory_mb=512` added to the kernel command line before this persistent setting takes effect.

For a PAM-login shell, raise `memlock` for the capture user or group with `/etc/security/limits.d/domesday.conf`:

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

Use a bare name such as `captureuser` for a user-specific limit, or an `@` prefix such as `@domesday` for a group limit. Existing sessions keep their inherited limits, so start a fresh login session before re-checking `ulimit -l` and `ulimit -r`. If a desktop-launched app does not inherit the configured limit, launch it from a fresh login shell or configure its launcher/service limit explicitly. For a systemd service, set `LimitMEMLOCK=512M` and `LimitRTPRIO=80` in the service unit.

If capture prints:

```text
warning: SetCurrentThreadRealtimePriority: Unable to set thread priority
```

the process could not raise its capture threads to realtime scheduler priority. This does not mean the capture failed. It means the kernel kept the process at normal scheduling priority, so capture is more exposed to CPU scheduling delays while the USB transfer and disk writer threads are trying to keep up.

Short captures may still complete successfully with this warning. For long captures, busy systems, slow disks, or machines doing other work, fix the realtime limit before treating the host as fully tuned.

Check the active realtime priority limit in the same shell that will run `dddcli`:

```sh
ulimit -r
```

Expected: `80` or higher. If it reports `0`, the user cannot request realtime priority from that shell.

For a normal PAM login session, add an `rtprio` limit for the capture user or a capture group in `/etc/security/limits.d/domesday.conf`:

```text
captureuser soft rtprio 80
captureuser hard rtprio 80
@domesday soft rtprio 80
@domesday hard rtprio 80
```

Then start a fresh login session and check again:

```sh
ulimit -r
```

Existing terminals, long-running desktop sessions, and already-started services keep the old inherited limit. If `ulimit -r` still reports `0` after adding the limits file, common causes are:

- the command is running from an old terminal that was open before the limit changed
- the user is not a member of the configured group
- the limits file uses `@group` syntax for a user name, or a bare name for a group
- the launcher is not PAM-backed or does not inherit the shell's limits
- a systemd service or user service has its own stricter limit

For a systemd service, set the realtime priority limit in the service unit:

```ini
[Service]
LimitRTPRIO=80
```

For a systemd user service, also check the user manager's inherited limits. A service-level `LimitRTPRIO=80` is the clearest way to make the capture environment reproducible.

After changing service limits, reload and restart the service:

```sh
systemctl daemon-reload
systemctl restart your-capture-service
```

If the warning remains but the JSON sidecar reports `transferResultString` as `success`, the run completed despite normal scheduling priority. Record the warning with the capture results, especially when comparing long-run stability or investigating dropped/corrupt data.

## What To Record For Bugs

Keep:

- exact command line
- stdout and stderr
- config file used, if any
- player model/version/profile from `player status`
- raw serial responses for relevant commands
- disc type and capture mode
- JSON sidecar from the capture
- notes about what the physical player did
