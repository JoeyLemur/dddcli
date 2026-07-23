# Auto Capture

`auto-capture` coordinates USB capture with a serial-controlled LaserDisc player. It detects the loaded disc type, verifies it against `--disc-type` when an override is supplied, positions the player, starts USB capture, starts playback, monitors the player address, then stops capture and writes metadata. Whole-disc and lead-in captures also probe the disc end address during setup.

## Modes

- `whole-disc`: capture from spin-down/lead-in to the detected end address.
- `lead-in`: capture from spin-down/lead-in to the requested or detected end address.
- `partial`: seek to `--start-address` and capture until `--end-address`.

`partial` requires `--end-address`, and the normalized end address must be greater than the start address.

For `whole-disc`, the detected disc end is always used. For `lead-in`, a supplied `--end-address` is capped to the detected disc end; without an end address, the detected disc end is used. `partial` uses the requested `--end-address` directly and does not perform the disc-end probe. To detect the end address for whole-disc and lead-in modes, CAV captures seek to frame `60000`, while CLV captures seek to `1:59:59` before reading the player-reported address.

Whole-disc CLV capture starts from spin-down/lead-in and stops at the detected player-reported end timecode. It does not require the first playable CLV timecode to be `0:00:00`; a disc that begins at a later displayed timecode is still tracked by the actual addresses returned by the player.

For `whole-disc` and `lead-in`, setup verifies the player is stopped immediately before USB capture starts. If disc probing or earlier manual use left the player spun up, paused, or playing, the CLI sends stop first so RF capture begins before the next spin-up/play command. `partial` captures seek to the requested start address and begin capture from that positioned playback point instead.

Auto-capture turns the player's on-screen display on by default with `1DS` before positioning and playback. Use `--no-on-screen-display` or `[auto_capture] on_screen_display = false` to send `0DS` instead when you want the player display disabled during capture.

Some older CLV discs only expose hour/minute timecode precision. If the detected CLV disc end lands exactly on a minute boundary and the capture range uses that detected end, the CLI treats the end as possibly minute-granular and uses a 61 second end post-roll instead of the normal second-granular post-roll. This avoids cutting off most of the final minute; the capture can still finish earlier if the player stops, pauses, or still-frames during that post-roll.

If a CLV capture reaches the end of its requested range and the player rolls over to an earlier timecode instead of stopping, the CLI treats that wraparound as the end of the range and stops capture.

## Disc Type

`--disc-type cav|clv` is optional. When it is omitted, the CLI asks the player for the loaded disc type and uses that detected type for address handling and metadata. When it is supplied, the CLI still asks the player for the loaded disc type and fails if the player response does not match the requested type.

CAV uses frame addresses. CLV uses player-reported timecodes normalized to seconds internally.

## CAV Stop Behavior

CAV captures stop when the reported frame address reaches or passes the end address:

```text
address >= endAddress
```

A CAV frame is an exact frame address, so there is no CLV-style need to continue capturing through the rest of a displayed second. Polling latency can capture a little extra data, but it should not drop the requested final frame.

If a CAV capture falls into still-frame during capture, the CLI attempts to resume play.

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

All CLV forms normalize to seconds from the displayed player timecode. `754`, `01234`, and `0123400` are equivalent.

These CLV values are absolute player timecodes, not offsets relative to the first playable timecode on a particular disc. For example, if a disc starts at displayed timecode `0:05:00`, use `00500` or `300` to refer to that point in `partial` or `lead-in` ranges.

When reading player responses, the CLI also accepts compact `HMM`, such as `012`, from minute-only CLV timecodes and normalizes it to the first second of that displayed minute. User-supplied values below five digits are treated as seconds.

## Metadata

Auto-capture metadata includes player model, player version, serial speed, disc type, disc status, and observed address bounds from the active capture loop. Disc-end probe addresses are not included in the captured min/max fields. If a near-end CLV wrap triggers capture completion, the wrapped restart address is also excluded from the min/max range.

The sidecar always includes a `captureInfo` object with the capture file path, capture format, test-mode flag, transfer result, duration, transfer and buffer counts, file size, sample statistics, clipping statistics, sequence marker presence, and UTC creation timestamp.

For CAV captures:

- `minFrameNumber`
- `maxFrameNumber`

For CLV captures:

- `minTimeCode`
- `maxTimeCode`

CLV metadata values are normalized seconds from the displayed player timecode, not raw compact `HMM`, `HMMSS`, or `HMMSSFF` strings. The precision reflects the values reported by the player, so minute-only CLV discs may record `minTimeCode` and `maxTimeCode` on minute boundaries.

### Future capture quality metrics

A future metadata addition could expose lightweight RF quality indicators for capture smoke tests, such as richer sample distribution statistics, dropout/flatline hints, or per-second stability summaries. Any live-capture metrics must be observe-only, cheap to update while buffers are already being handled, and must never block USB transfer handling or disk writes. Heavier analysis, including decode-like checks, FFTs, histograms, or calibration diagnostics, should live in an offline command that reads an existing capture file instead of running in the active capture path.

## Cleanup

The auto-capture path is designed to clean up on success, capture errors, and interruption:

- USB transfer is stopped if still running.
- key lock is released if it was enabled. If requested key lock cannot be enabled, capture aborts before USB capture starts.
- setup interruptions before USB capture starts abort without starting playback.
- successful CAV and CLV captures end by stopping the player.
- auto-capture address errors leave the player in still-frame for inspection.
- CAV still-frame resume failures stop capture and leave the player in still-frame for inspection.
- `SIGINT` and `SIGTERM` request an orderly stop; capture cleanup still runs before the process exits.

If the final player cleanup command or key-lock release fails after capture, the CLI reports the failure and exits non-zero.

Manual `capture` uses the same USB cleanup and metadata writer, but it does not add player or disc fields to `serialInfo`.
