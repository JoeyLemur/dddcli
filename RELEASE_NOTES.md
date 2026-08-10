# Release notes

## v1.1.2 — 2026-08-10

This release extends reliable auto-capture end detection to CLV discs and makes
end-of-disc reporting clearer.

- CLV end probing now requires a stable terminal timecode after the far seek, then verifies a backward end transition or a terminal state after playback advances past that floor.
- A CLV player that rejects the far seek while stopped is spun up and retried once before capture is allowed to start.
- CAV and CLV console output now reports concise verified/detected end transitions instead of noisy post-end address changes.
- When an end transition completes capture, the CLI suppresses the misleading post-transition `Last observed` address; normal capture stops still report it.
- The CLV path was hardware-validated on a Pioneer LD-V2200, including two successful whole-side captures.

## v1.1.1 — 2026-08-10

This release makes CAV auto-capture reliably detect the physical end of discs
whose players stop early at a false end-frame address.

- CAV end probing now verifies a frame rollover while playing with stop codes disabled, rather than treating a still-frame result from the `FR60000` seek as the disc end.
- Whole-disc and unbounded lead-in CAV captures continue past the probe floor and stop at the verified rollover; bounded CAV lead-in captures fail clearly if rollover happens before the requested end frame.
- Final auto-capture reporting now labels the last serial result as the "Last observed" address, avoiding an implication that it is the physical final frame.
- CAV rollover behavior, range resolution, user documentation, and manual hardware test expectations have been expanded accordingly.

## v1.1.0 — 2026-08-05

This release simplifies capture output and improves auto-capture behavior.

- Captures now always use packed 10-bit LDS output; `--format` and `capture.format` have been removed.
- Relative `--output` paths now respect `--output-dir`.
- `--duration` now stops auto-captures, and auto-capture reports its last valid player position.
- The default config location is now `${XDG_CONFIG_HOME}/dddcli/dddcli.toml` or `~/.config/dddcli/dddcli.toml`.

## v1.0.1 — 2026-07-23

This release makes capture setup safer and the CLI guidance clearer.

- Disk buffer queues below 6 MiB are rejected before hardware access.
- USB capture startup and descriptor handling are more robust when devices or files fail unexpectedly.
- Configuration, auto-capture, and troubleshooting documentation now more closely reflects actual CLI behavior.
