# Troubleshooting

## Build Fails

Run:

```sh
cmake -S . -B build
```

If configuration fails, check:

- libusb development files are installed
- `pkg-config` can find `libusb-1.0`

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

For Linux USB access, complete the [Linux capture host setup](LINUX_CAPTURE_HOST_SETUP.md#1-allow-non-root-usb-access). It contains the udev rule, how to apply it, and how to verify the final device-node permissions.

If the Duplicator only appears after a manual poke or unplug/replug, first check whether the kernel saw it during boot:

```sh
sudo journalctl -b -k --no-pager | grep -Ei '1d50|603b|Domesday|usb [0-9]+-[0-9]+'
```

If the boot log shows `idVendor=1d50, idProduct=603b`, the hardware enumerated and the likely permanent fix is the late udev rule in the [Linux capture host setup](LINUX_CAPTURE_HOST_SETUP.md#1-allow-non-root-usb-access). If the boot log does not show the Duplicator at all, investigate USB power, cable/port, hub behavior, firmware startup timing, or xHCI/controller quirks before chasing permissions.

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

For Linux serial-device permissions, see [Allow Serial-Device Access](LINUX_CAPTURE_HOST_SETUP.md#4-allow-serial-device-access).

## Loaded Disc Reports Unknown Type

`player status` does not spin up or seek the disc just to identify CAV versus CLV. Some players, including the LD-V2200, can report a loaded but stopped disc as `discStatus=1XXXX` and `discType=unknown`. This is different from the no-disc baseline such as `discStatus=0XXXX`.

If you need to confirm the disc type manually, start playback or run an auto-capture mode that already spins and positions the player as part of its normal setup. After playback starts, status should report `discType=CAV` or `discType=CLV` if the player exposes it.

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

Some older CLV discs only expose hour/minute precision. If whole-disc capture detects an end timecode on an exact minute boundary, the CLI waits up to 61 seconds after first seeing that end address so it does not drop the rest of the final minute. It still stops early if the player reaches a terminal state during that post-roll.

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
- on Linux, use the [Linux capture host setup](LINUX_CAPTURE_HOST_SETUP.md) if startup reports `USB memory limit`, `LIBUSB_ERROR_NO_MEM`, `RLIMIT_MEMLOCK`, or `mlock failed`
- use `--small-usb-transfer-queue` only when the host cannot be tuned
- if writes cannot keep up, keep `--small-usb-transfers` enabled and try faster local storage
- keep the JSON sidecar; it records the transfer result and sample statistics

If `--json` points to a path in a missing directory, the CLI creates that directory before writing the sidecar.

## Capture Progress In Logs

When stderr is attached to a terminal, capture progress is shown as a single updating line. When stderr is redirected or piped through a command such as `tee`, progress is written as newline-delimited status about every 10 seconds so long captures still leave useful log evidence. Auto-capture progress also includes the current player position as `timecode=H:MM:SS` for CLV or `frame=<n>` for CAV after the first address is read. `--quiet` suppresses both forms of progress output.

## Linux Capture Host Errors

The one-time [Linux capture host setup](LINUX_CAPTURE_HOST_SETUP.md) covers USB permissions, USBFS memory, and the `memlock`/`rtprio` limits. For a startup error such as `USB memory limit`, `LIBUSB_ERROR_NO_MEM`, `RLIMIT_MEMLOCK`, or `mlock failed`, return to its checks and make the persistent configuration changes before retrying the default queue.

If capture prints:

```text
warning: SetCurrentThreadRealtimePriority: Unable to set thread priority
```

the process could not raise its capture threads to realtime scheduler priority. This does not mean the capture failed. It means the kernel kept the process at normal scheduling priority, so capture is more exposed to CPU scheduling delays while the USB transfer and disk writer threads are trying to keep up.

Short captures may still complete successfully with this warning. For long captures, busy systems, slow disks, or machines doing other work, finish the `rtprio` setup before treating the host as fully tuned. If the warning remains but the JSON sidecar reports `transferResultString` as `success`, the run completed despite normal scheduling priority. Record the warning with the capture results, especially when comparing long-run stability or investigating dropped/corrupt data.

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
