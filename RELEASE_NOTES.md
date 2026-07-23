# Release notes

## v1.0.1 — 2026-07-23

This release makes capture setup safer and the CLI guidance clearer.

- Disk buffer queues below 6 MiB are rejected before hardware access.
- USB capture startup and descriptor handling are more robust when devices or files fail unexpectedly.
- Configuration, auto-capture, and troubleshooting documentation now more closely reflects actual CLI behavior.
