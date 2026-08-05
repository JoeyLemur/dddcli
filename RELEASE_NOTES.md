# Release notes

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
