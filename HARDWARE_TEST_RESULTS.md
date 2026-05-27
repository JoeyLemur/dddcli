# Hardware Test Results

This file records real hardware validation runs for `dddcli`. Keep entries concise but exact enough to show what was tested, what passed, and what still needs work.

## 2026-05-26 - LD-V2200 No-Disc Serial And USB Preflight

### Setup

- Host timezone: America/Chicago
- Build tree: `/home/epowell/Development/dddcli/build`
- Player: Pioneer LD-V2200
- Serial device: `/dev/ttyUSB0`
- Serial speed: 4800 baud, with one auto-speed confirmation
- Disc state: no disc loaded
- DdD USB device: connected

### Fresh Build And Software Tests

| Test | Command | Result | Notes |
| --- | --- | --- | --- |
| Configure | `cmake -S . -B build` | PASS | CMake configure/generate completed. |
| Build | `cmake --build build` | PASS | `dddcli` and `dddcli_tests` targets built. No recompilation was needed. |
| Software tests | `ctest --test-dir build --output-on-failure` | PASS | 1/1 tests passed. |

### USB Preflight

| Test | Command | Result | Notes |
| --- | --- | --- | --- |
| DdD USB visibility | `./build/dddcli list-devices` | PASS | Reported `/sys/bus/usb/devices/4-1`. |
| USB speed | `cat /sys/bus/usb/devices/4-1/speed` | PASS | Reported `5000`, expected USB 3 SuperSpeed. |
| USB version | `cat /sys/bus/usb/devices/4-1/version` | PASS | Reported `3.00`. |
| USBFS memory | `cat /sys/module/usbcore/parameters/usbfs_memory_mb` | PASS | Reported `512`. |
| Locked memory limit | `ulimit -l` | PASS | Reported `524288` KiB. |

### Serial Device Preflight

| Test | Command | Result | Notes |
| --- | --- | --- | --- |
| Serial device exists | `ls -l /dev/ttyUSB0` | PASS | `crw-rw---- 1 root dialout 188, 0 ... /dev/ttyUSB0`. |
| Sandboxed serial access | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800` | BLOCKED | Inside the sandbox, opening `/dev/ttyUSB0` failed with `No such file or directory`. Direct device access was required for player tests. |

### Player Detection And Profile Selection

| Test | Command | Result | Observed output |
| --- | --- | --- | --- |
| Explicit 4800 status | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | Connected to Pioneer LD-V2200 version 02 @ 4800 bps using `pioneer-ld-v2200`; `state=stop`; `discType=unknown`; `discStatus=0XXXX`. |
| Default player action | `./build/dddcli player --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | Same status output as explicit `status`. |
| Auto speed detection | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed auto` | PASS | Detected 4800 bps and selected `pioneer-ld-v2200`. |
| Manual LD-V2200 profile | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `playerProfile=pioneer-ld-v2200`. |
| Manual LD-V4300D profile | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v4300d` | PASS | `playerProfile=pioneer-ld-v4300d`. |
| Manual generic profile | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile generic-level3` | PASS | `playerProfile=generic-level3`. |

### Raw Serial Evidence, No Disc Loaded

All commands exited successfully and connected as Pioneer LD-V2200 version 02 @ 4800 bps using the `pioneer-ld-v2200` profile.

| Raw command | Result | Escaped response | Notes |
| --- | --- | --- | --- |
| `?X` | PASS | `P150702\r` | Model detection evidence. Model code `07`, version `02`. |
| `?P` | PASS | `P01\r` | Stop state. |
| `?D` | PASS | `0XXXX\r` | No-disc baseline status. |
| `?F` | PASS | `E04\r` | No-disc address query baseline. |
| `?T` | PASS | `E04\r` | No-disc timecode query baseline. CLV timecode width still needs disc-loaded validation. |
| `?U` | PASS | `E04\r` | No-disc Pioneer user-code query baseline. |
| `$Y` | PASS | `E04\r` | No-disc baseline for inherited/legacy standard user-code query. This command may be model-specific. |

### Not Tested Yet

- Basic player controls with a disc loaded: `play`, `pause`, `still`, `stop`, plus status after each.
- CLV timecode width on LD-V2200 with a CLV disc loaded.
- CAV frame reporting with a CAV disc loaded.
- `read-user-codes` with a disc that has readable user-code data.
- Partial CLV auto-capture with real USB capture data.
- Whole-disc CLV end detection and post-roll behavior.
- Minute-only CLV disc behavior, if an appropriate disc is available.
- Partial CAV auto-capture and frame-bound stop behavior.

### Needs Work / Follow-Up

- Continue hardware tests with a non-valuable test disc loaded.
- Record whether LD-V2200 `?T` returns `HMM`, `HMMSS`, or another width during CLV playback.
- Record whether no-disc `E04\r` responses should be surfaced more explicitly in user-facing docs for raw address/user-code probes.
- Keep direct serial access enabled for future player tests; sandboxed access cannot open `/dev/ttyUSB0`.

## 2026-05-26 - LD-V2200 CLV Disc-Loaded Serial Tests

### Setup

- Player: Pioneer LD-V2200
- Serial device: `/dev/ttyUSB0`
- Serial speed: 4800 baud
- Disc state: CLV disc loaded
- Expected disc timecode: normal hours-minutes-seconds

### Disc Detection

| Test | Command | Result | Observed output |
| --- | --- | --- | --- |
| Status after loading disc | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `state=stop`; `discType=unknown`; `discStatus=1XXXX`. |
| Raw disc status | `./build/dddcli player raw-command '?D' --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `1XXXX\r`. |

Notes:

- Loaded no-play status changed from the no-disc baseline `0XXXX\r` to `1XXXX\r`.
- The parser still reported `discType=unknown` while stopped immediately after loading, but later reported `discType=CLV` once playback started.

### CLV Timecode Width

| Test | Command | Result | Escaped response |
| --- | --- | --- | --- |
| Play disc | `./build/dddcli player play --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | Command eventually exited successfully after a noticeable spin-up/start delay. |
| Playback status | `./build/dddcli player status --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `state=play`; `discType=CLV`; `discStatus=11001`. |
| LD-V2200 profile timecode | `./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `00002\r`. |
| LD-V4300D profile timecode | `./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v4300d` | PASS | `00002\r`. |
| Generic profile timecode | `./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile generic-level3` | PASS | `00011\r`. |

Conclusion: this LD-V2200 returns 5-digit `HMMSS` CLV timecode during playback on this disc.

### Raw Address And User-Code Probes During CLV Playback

| Test | Command | Result | Escaped response |
| --- | --- | --- | --- |
| CAV frame query on CLV disc | `./build/dddcli player raw-command '?F' --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `E04\r`. |
| Pioneer user-code query | `./build/dddcli player raw-command '?U' --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `E04\r`; per the Level III manual, `?U` returns `E04` when no data is encoded in the Pioneer User's Code. |
| Inherited standard user-code query | `./build/dddcli player raw-command '$Y' --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `E04\r`. `$Y` came from the inherited GUI code and may be model-specific; it has not been matched to the LD-V2200 Level III manual yet. |
| Read user codes action | `./build/dddcli player read-user-codes --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | `standardUserCode=E04`; `pioneerUserCode=E04`. The Pioneer value is a documented no-data response for this disc; the standard value is legacy/model-specific evidence. |

### Basic Player Controls

| Test | Command | Result | Notes |
| --- | --- | --- | --- |
| Play | `./build/dddcli player play --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | Command exited successfully after spin-up/start delay; follow-up status reported `state=play`. |
| Pause | `./build/dddcli player pause --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | Follow-up status reported `state=pause`; `discType=CLV`; `discStatus=11001`. |
| Still command from pause | `./build/dddcli player raw-command 'ST' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | FAIL | Raw `ST` returned `E04\r` while paused on the CLV disc. Follow-up status remained `state=pause`. The Level III manual lists `ST` as valid from Play Mode, so this failure is expected for the wrong starting state. |
| Still command from play | `./build/dddcli player play --serial-device /dev/ttyUSB0 --serial-speed 4800`; then `./build/dddcli player raw-command 'ST' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | FAIL | Follow-up status before `ST` reported `state=play`; raw `?T` returned `00135\r`; raw `ST` still returned `E04\r`. Needs more investigation against the manual's Random Access Mode/CLV notes. |
| Stop | `./build/dddcli player stop --serial-device /dev/ttyUSB0 --serial-speed 4800` | PASS | Command exited successfully after a short delay; follow-up status reported `state=stop`; `discType=CLV`; `discStatus=11001`. |

### Auto-Capture

| Test | Command | Result | Notes |
| --- | --- | --- | --- |
| Short partial CLV auto-capture | `./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --serial-speed 4800 --disc-type clv --mode partial --start-address 60 --end-address 70 --output /tmp/dddcli-clv-ldv2200-20260526.lds --json /tmp/dddcli-clv-ldv2200-20260526.json` | BLOCKED | Direct hardware access approval for the auto-capture command was rejected, so no capture file or JSON sidecar was written. |
| Capture target free space | `df -h /home/tmp` | PASS | `/home/tmp` is on `/dev/nvme1n1p1` with 1.9T available. |
| Short partial CLV auto-capture to capture drive, before seek/probe fixes | `./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --serial-speed 4800 --disc-type clv --mode partial --start-address 60 --end-address 70 --output /home/tmp/dddcli-clv-ldv2200-20260526-60-70.lds --json /home/tmp/dddcli-clv-ldv2200-20260526-60-70.json` | FAIL | Connected to the player, then failed before capture with `Could not determine CLV disc length`. No `.lds` or `.json` file was created. Follow-up status showed the player left in `state=play`; it was manually stopped afterward. |
| Short partial CLV auto-capture to capture drive, after seek/probe fixes and before final-stop cleanup | Same command as above | PASS | Capture completed successfully. Output `.lds` size was 720M. JSON sidecar was 858 bytes. CLI reported `Capture complete: success` and wrote JSON metadata. Follow-up status reported `state=pause`; `discType=CLV`; `discStatus=11001`. This was before successful CLV cleanup was changed to stop the player. |
| Raw LD-V2200 impossible-end probe | `./build/dddcli player raw-command 'TM15959SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | Returned `R\r`; follow-up `?T` returned `04448\r`. |
| Bounded lead-in CLV auto-capture exercising disc-end probe, before timecode-read retry | `./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --serial-speed 4800 --disc-type clv --mode lead-in --end-address 70 --output /home/tmp/dddcli-clv-ldv2200-20260526-leadin-70.lds --json /home/tmp/dddcli-clv-ldv2200-20260526-leadin-70.json` | FAIL | Failed with `Could not determine CLV disc length` even though the same `TM15959SE` plus `?T` sequence worked through `raw-command`. No `.lds` or `.json` file was created. |
| Bounded lead-in CLV auto-capture exercising disc-end probe, after timecode-read retry and before final-stop cleanup | Same command as above | PASS | Capture completed successfully. Output `.lds` size was 4.1G. JSON sidecar was 865 bytes. CLI reported `Capture complete: success` and wrote JSON metadata. Follow-up status reported `state=pause`; `discType=CLV`; `discStatus=11001`. This was before successful CLV cleanup was changed to stop the player. |
| Post-test player spin-down before final-stop cleanup | `./build/dddcli player stop --serial-device /dev/ttyUSB0 --serial-speed 4800`; then status | PASS | Player was left in `state=pause` after the then-current CLV capture cleanup; manual stop succeeded and follow-up status reported `state=stop`; `discType=CLV`; `discStatus=11001`. Current successful CLV cleanup stops the player automatically. |
| Whole-disc CLV auto-capture, first long run | `./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --serial-speed 4800 --disc-type clv --mode whole-disc --output /home/tmp/dddcli-clv-ldv2200-whole-disc-20260526.lds --json /home/tmp/dddcli-clv-ldv2200-whole-disc-20260526.json` | FAIL | Disc reached the end, restarted from the beginning, and capture continued until manually stopped or terminated. Output `.lds` grew to 129G. No JSON sidecar was written. Log only contained startup messages from the pre-periodic-progress build. Follow-up status reported `state=play`; manual `player stop` then returned the player to `state=stop`. |
| Near-end partial CLV wrap-stop auto-capture before final-stop cleanup | `./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --serial-speed 4800 --disc-type clv --mode partial --start-address 04420 --end-address 04448 --output /home/tmp/dddcli-clv-ldv2200-endwrap-partial-20260526.lds --json /home/tmp/dddcli-clv-ldv2200-endwrap-partial-20260526.json` | PASS | Capture emitted redirected progress with `timecode=...`, detected `CLV time code wrapped from 2687 to 2683 near requested end`, stopped capture cleanly, reported `Capture complete: success`, and wrote JSON metadata. Output `.lds` size was 1.5G; JSON sidecar was 865 bytes. Cleanup printed `Failed to pause player during cleanup`, but immediate follow-up status reported `state=pause`; `discType=CLV`; `discStatus=11001`. This was before successful CLV cleanup was changed to stop the player. |
| Near-end partial CLV wrap-stop with final stop cleanup and metadata guard | `./build/dddcli auto-capture --serial-device /dev/ttyUSB0 --serial-speed 4800 --disc-type clv --mode partial --start-address 04420 --end-address 04448 --output /home/tmp/dddcli-clv-ldv2200-endwrap-stop-metadata-20260526.lds --json /home/tmp/dddcli-clv-ldv2200-endwrap-stop-metadata-20260526.json` | PASS | Capture detected `CLV time code wrapped from 2688 to 2120 near requested end`, stopped capture cleanly, reported `Capture complete: success`, and wrote JSON metadata. Output `.lds` size was 1.6G; JSON sidecar was 871 bytes. Follow-up status reported `state=stop`; `discType=CLV`; `discStatus=11001`. JSON did not include the wrapped restart address in the min/max range. |

Successful capture JSON highlights:

- `serialInfo.playerModelCode`: `P150702`
- `serialInfo.playerModelName`: `Pioneer LD-V2200`
- `serialInfo.serialSpeed`: `4800`
- `serialInfo.discType`: `CLV`
- `serialInfo.discStatus`: `11001`
- `serialInfo.minTimeCode`: `60`
- `serialInfo.maxTimeCode`: `71`
- `captureInfo.transferResultString`: `success`
- `captureInfo.durationInMilliseconds`: `15269`
- `captureInfo.fileSizeWrittenInBytes`: `754974720`
- `captureInfo.sampleCount`: `603979776`
- `captureInfo.sequenceMarkersPresent`: `true`

Successful bounded lead-in JSON highlights:

- `serialInfo.playerModelCode`: `P150702`
- `serialInfo.playerModelName`: `Pioneer LD-V2200`
- `serialInfo.serialSpeed`: `4800`
- `serialInfo.discType`: `CLV`
- `serialInfo.discStatus`: `11001`
- `serialInfo.minTimeCode`: `0`
- `serialInfo.maxTimeCode`: `71`
- `captureInfo.transferResultString`: `success`
- `captureInfo.durationInMilliseconds`: `87153`
- `captureInfo.fileSizeWrittenInBytes`: `4348968960`
- `captureInfo.sampleCount`: `3479175168`
- `captureInfo.sequenceMarkersPresent`: `true`

Successful near-end partial wrap-stop JSON highlights:

- `serialInfo.playerModelCode`: `P150702`
- `serialInfo.playerModelName`: `Pioneer LD-V2200`
- `serialInfo.serialSpeed`: `4800`
- `serialInfo.discType`: `CLV`
- `serialInfo.discStatus`: `11001`
- `serialInfo.minTimeCode`: `2660`
- `serialInfo.maxTimeCode`: `2687`
- `captureInfo.transferResultString`: `success`
- `captureInfo.durationInMilliseconds`: `31945`
- `captureInfo.fileSizeWrittenInBytes`: `1588592640`
- `captureInfo.sampleCount`: `1270874112`
- `captureInfo.sequenceMarkersPresent`: `true`

Successful near-end partial final-stop JSON highlights:

- `serialInfo.playerModelCode`: `P150702`
- `serialInfo.playerModelName`: `Pioneer LD-V2200`
- `serialInfo.serialSpeed`: `4800`
- `serialInfo.discType`: `CLV`
- `serialInfo.discStatus`: `11001`
- `serialInfo.minTimeCode`: `2660`
- `serialInfo.maxTimeCode`: `2688`
- `captureInfo.transferResultString`: `success`
- `captureInfo.durationInMilliseconds`: `32702`
- `captureInfo.fileSizeWrittenInBytes`: `1626603520`
- `captureInfo.sampleCount`: `1301282816`
- `captureInfo.sequenceMarkersPresent`: `true`

### CLV Seek Evidence

| Test | Command | Result | Escaped response |
| --- | --- | --- | --- |
| Raw current time after failed capture/seek checks | `./build/dddcli player raw-command '?T' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `00017\r`. |
| Raw 5-digit CLV seek to 1:00 while stopped | `./build/dddcli player raw-command 'FR00100SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `E04\r`. |
| Raw 5-digit CLV seek to 1:00 while playing | `./build/dddcli player raw-command 'FR00100SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `E04\r`. |
| Raw 7-digit CLV seek to 1:00 while playing | `./build/dddcli player raw-command 'FR0010000SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `E04\r`. |
| Raw 5-digit CLV impossible-end seek | `./build/dddcli player raw-command 'FR15959SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `E04\r`. |
| Raw LD-V2200 `TM` CLV seek to 1:00 while playing | `./build/dddcli player raw-command 'TM00100SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `R\r`. |
| Raw LD-V2200 `TM` impossible-end seek | `./build/dddcli player raw-command 'TM15959SE' --serial-device /dev/ttyUSB0 --serial-speed 4800 --player-profile pioneer-ld-v2200` | PASS | `R\r`; follow-up `?T` returned `04448\r`. |

Code changes validated by the successful capture:

- LD-V2200 CLV seeks now use `TM<HMMSS>SE` instead of `FR<HMMSS>SE`.
- Partial auto-capture skips the whole-disc end probe and uses the required `--end-address`, avoiding the LD-V2200 impossible-end seek failure.
- CLV disc-end reads after the impossible-end seek now retry briefly; this allowed the bounded lead-in auto-capture to detect the end and proceed.
- Lead-in and whole-disc auto-capture now check player state immediately before USB capture and spin down if needed, so capture starts before the next spin-up/play command.
- CLV auto-capture now treats a near-end timecode wrap back to an earlier timecode as the end of the requested range, preventing players that restart from continuing capture indefinitely and allowing near-end partial tests to exercise the same behavior.
- Successful auto-capture cleanup now stops CLV discs instead of pausing them, so the player is left spun down after capture.
- Wrapped CLV restart addresses are checked before metadata recording, so the post-wrap address does not pollute `minTimeCode`/`maxTimeCode`.
- `ctest --test-dir build --output-on-failure` passed after these changes.

### Needs Work / Follow-Up

- Investigate whether `player still` failing on a CLV disc is expected LD-V2200 behavior, a profile command mismatch, or a state/precondition issue.
- Re-run a full CLV whole-disc capture with the current wrap-stop, metadata guard, redirected progress, and final-stop cleanup.
- Continue with a longer CLV partial capture and then a whole-disc CLV test if storage/time allows.
- Consider documenting that loaded-but-stopped CLV status may initially report `discType=unknown` until playback status is queried.
