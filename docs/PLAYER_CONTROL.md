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
- `$Y`: standard user code query.
- `?U`: Pioneer user code query.
- `PL`: play.
- `PL64RBMF`: play with stop codes disabled.
- `PA`: pause.
- `ST`: still frame.
- `RJ`: stop/reject.
- `1KL`: key lock on.
- `0KL`: key lock off.
- `FR<address>SE`: seek to frame or timecode address.

The LD-V2200 profile formats CLV seek addresses as 5-digit `HMMSS`. The generic and LD-V4300D profiles format CLV seek addresses as 7-digit `HMMSSFF`.

## CLV Timecode Parsing

CLV responses are parsed tolerantly:

- `HMMSS`, such as `01234`, is accepted.
- `HMMSSFF`, such as `0123400`, is accepted and the frame digits are ignored.
- leading `<` and `>` markers are preserved in raw output and tolerated by parsed address reads.

Parsed CLV addresses are normalized to elapsed seconds. For example, both `01234` and `0123400` normalize to `754`.

The LD-V2200 has been observed returning 5-digit CLV timecodes over serial. The LD-V4300D 7-digit behavior is an inherited assumption until verified on real hardware.

## Raw Serial Probing

Use `player raw-command` to collect exact player responses without adding new code:

```sh
./build/dddcli player raw-command '?T' \
  --serial-device /dev/ttyUSB0 \
  --player-profile pioneer-ld-v2200
```

The command appends carriage return if needed. Output is escaped so carriage returns, newlines, tabs, backslashes, and non-printable bytes can be recorded safely.

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

## Status Output

`dddcli player status` prints:

- `model`
- `playerProfile`
- `state`
- `discType`
- `discStatus`

Use `playerProfile` to confirm whether `auto` selected the intended command profile.
