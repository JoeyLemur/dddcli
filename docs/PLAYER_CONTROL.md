# Player Control

`dddcli` controls Pioneer-compatible LaserDisc players through a serial Level III-style command interface. The current implementation is intentionally profile-based because different players and inherited code disagree about some details, especially CLV timecode width.

## Player Profiles

Use `--player-profile` or `[player] profile` to choose a profile:

- `auto`: detect the model code and select a profile.
- `generic-level3`: use generic Pioneer Level III-compatible commands.
- `pioneer-ld-v4300d`: use the inherited LD-V4300D-oriented assumptions.
- `pioneer-ld-v2200`: use LD-V2200-compatible CLV timecode formatting.

Automatic profile selection is based on the model code returned by `?X`:

- model code `15`: `pioneer-ld-v4300d`
- model code `07`: `pioneer-ld-v2200`
- unknown compatible model: `generic-level3`

Manual profile selection overrides model-based selection.

## Commands Used

The profiles currently share most command strings:

- `?X`: model and version query used during connection.
- `?P`: player state query.
- `?D`: disc status and disc type query.
- `?F`: current CAV frame query.
- `?T`: current CLV timecode query.
- `$Y`: inherited/legacy standard user code query from the original GUI code. This may be model-specific; it is not currently verified against the LD-V2200 Level III manual.
- `?U`: Pioneer user code query.
- `PL`: play.
- `PL64RBMF`: play with stop codes disabled.
- `PA`: pause.
- `ST`: still frame. The Level III manual lists this as valid from Play Mode.
- `RJ`: stop/reject.
- `1KL`: key lock on.
- `0KL`: key lock off.
- `1DS`: on-screen display on.
- `0DS`: on-screen display off.
- `FR<address>SE`: seek to CAV frame address; generic and LD-V4300D CLV profiles also use this for timecode seeks.
- `TM<address>SE`: LD-V2200 CLV timecode seek.

The LD-V2200 profile sends CLV seeks as `TM` plus 5-digit `HMMSS`. The generic and LD-V4300D profiles send CLV seeks as `FR` plus 7-digit `HMMSSFF`.

## CLV Timecode Parsing

CLV responses are parsed tolerantly:

- `HMM`, such as `012`, is accepted for minute-only CLV timecodes.
- `HMMSS`, such as `01234`, is accepted.
- `HMMSSFF`, such as `0123400`, is accepted and the frame digits are ignored.
- leading `<` and `>` markers are preserved in raw output and tolerated by parsed address reads.

Parsed CLV addresses are normalized to seconds from the displayed player timecode. For example, both `01234` and `0123400` normalize to `754`; minute-only `012` normalizes to `720`.

The LD-V2200 has been observed returning 5-digit CLV timecodes over serial and accepting `TM00100SE` for a 1-minute CLV seek. The LD-V4300D 7-digit behavior is an inherited assumption until verified on real hardware.

## Raw Serial Probing

Use `player raw-command` to collect exact player responses without adding new code:

```sh
./build/dddcli player raw-command '?T' \
  --serial-device /dev/ttyUSB0 \
  --player-profile pioneer-ld-v2200
```

The command appends carriage return if needed. Output is escaped so carriage returns, newlines, tabs, backslashes, and non-printable bytes can be recorded safely.

Raw commands are sent through the same serial connection setup as the other player actions. `--serial-speed auto` probes `9600`, `4800`, `2400`, then `1200`; specifying a speed tries only that speed.

Raw commands are limited to 20 bytes including the trailing carriage return that the CLI appends when the command does not already include one.

When validating a player, record raw responses for:

```sh
?X
?P
?D
?F
?T
?U
$Y
```

Keep the exact command, profile, serial speed, disc type, and raw escaped response with the hardware notes.

For `?U`, Pioneer documents `E04` as the response when no data is encoded in the Pioneer User's Code. Treat that as a valid no-data result, not a serial transport failure. `$Y` is retained for parity with the inherited GUI workflow, but should be treated as legacy/model-specific until verified on target hardware documentation.

## Status Output

`dddcli player` defaults to `status`. `dddcli player status` prints:

- `model`
- `playerProfile`
- `state`
- `discType`
- `discStatus`

Use `playerProfile` to confirm whether `auto` selected the intended command profile.
