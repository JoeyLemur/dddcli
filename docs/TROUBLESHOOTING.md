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

The defaults are VID `0x1D50` and PID `0x603B`.

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

CLV addresses are normalized to seconds internally. The CLI accepts:

- `754`
- `01234`
- `0123400`

All three mean 12 minutes and 34 seconds.

For raw hardware evidence, query:

```sh
./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0
```

Record whether the response is 5-digit `HMMSS`, 7-digit `HMMSSFF`, or has lead-in/lead-out markers.

## Auto-Capture Stops Late Or Early

CAV stops at the requested frame once the player reports that frame or later.

CLV intentionally captures past the first report of the requested end second. It stops after the player advances to the next second, after a 1.5 second post-roll timeout, or cleanly if the player stops/pauses/still-frames during that post-roll.

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
- keep the JSON sidecar; it records the transfer result and sample statistics

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
