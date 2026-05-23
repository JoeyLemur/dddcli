# Auto Capture

`auto-capture` coordinates USB capture with a serial-controlled LaserDisc player. It verifies the requested disc type, positions the player, starts USB capture, starts playback, monitors the player address, then stops capture and writes metadata.

## Modes

- `whole-disc`: capture from spin-down/lead-in to the detected end address.
- `lead-in`: capture from spin-down/lead-in to the requested or detected end address.
- `partial`: seek to `--start-address` and capture until `--end-address`.

`partial` requires `--end-address`, and the normalized end address must be greater than the start address.

## Disc Type

`--disc-type cav|clv` is required. The CLI also asks the player for the loaded disc type and fails if the player response does not match the requested type.

CAV uses frame addresses. CLV uses normalized elapsed seconds internally.

## CAV Stop Behavior

CAV captures stop when the reported frame address reaches or passes the end address:

```text
address >= endAddress
```

A CAV frame is an exact frame address, so there is no CLV-style need to continue capturing through the rest of a displayed second. Polling latency can capture a little extra data, but it should not drop the requested final frame.

If a CAV capture from lead-in falls into still-frame during capture, the CLI attempts to resume play.

## CLV Stop Behavior

CLV player addresses are second-granular. To avoid losing the tail of the requested final second, CLV capture does not stop on the first `address == endAddress`.

For CLV, capture stops when one of these happens:

- the player reports an address greater than the requested end second
- the player remains at the requested end second for 1.5 seconds after first reaching it
- after reaching the requested end second, the player reports `Stop`, `Pause`, or `StillFrame`
- USB capture ends or the user interrupts capture

If player state is `Unknown` during CLV post-roll, the CLI keeps relying on address advance, the 1.5 second post-roll timeout, USB transfer end, or existing address-read failure handling.

Terminal player states during CLV post-roll are treated as clean completion, not auto-capture errors. The capture still goes through normal cleanup: stop USB capture, write metadata, release key lock if enabled, and set the final player state.

## Address Forms

CAV start and end addresses are frame numbers.

CLV start and end addresses may be:

- seconds, such as `754`
- compact `HMMSS`, such as `01234`
- compact `HMMSSFF`, such as `0123400`

All CLV forms normalize to elapsed seconds. `754`, `01234`, and `0123400` are equivalent.

## Metadata

Auto-capture metadata includes player model, player version, serial speed, disc type, disc status, and observed address bounds.

For CAV captures:

- `minFrameNumber`
- `maxFrameNumber`

For CLV captures:

- `minTimeCode`
- `maxTimeCode`

CLV metadata values are normalized seconds, not raw compact `HMMSS` or `HMMSSFF` strings.

## Cleanup

The auto-capture path is designed to clean up on success, capture errors, and interruption:

- USB transfer is stopped if still running.
- key lock is released if it was enabled. If requested key lock cannot be enabled, capture aborts before USB capture starts.
- CAV captures end by stopping the player.
- CLV captures end by pausing the player.
- auto-capture address errors leave the player in still-frame for inspection.

If the final player cleanup command or key-lock release fails after capture, the CLI reports the failure and exits non-zero.
