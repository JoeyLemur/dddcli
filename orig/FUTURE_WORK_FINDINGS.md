# DomesdayDuplicator GUI Findings

This note summarizes the `tools/DomesdayDuplicator/` GUI application for future maintenance work.

## Scope

`tools/DomesdayDuplicator/` is the main Qt GUI for capturing LaserDisc RF data from Domesday Duplicator USB hardware and optionally coordinating a supported Pioneer LaserDisc player over serial. It is separate from the sibling tools `dddconv` and `dddutil`.

The application is GPLv3-or-later. QCustomPlot is vendored in-tree as `qcustomplot.cpp` and `qcustomplot.h`.

## Build And Dependencies

- Primary build path appears to be CMake from the repository root or `tools/`.
- Root `CMakeLists.txt` requires CMake 3.16, Qt 6.2+ `Core`, `Gui`, `Widgets`, `SerialPort`, and LibUSB.
- `tools/CMakeLists.txt` is a second entry point with similar Qt 6 and LibUSB requirements.
- `tools/DomesdayDuplicator/CMakeLists.txt` builds the `DomesdayDuplicator` executable from the Qt UI files, resources, USB backends, player control, and plotting code.
- The app targets C++20 in CMake. This is not just cosmetic: the USB pipeline uses C++20 atomic wait/notify APIs.
- A legacy qmake project file exists at `DomesdayDuplicator.pro`, but it is stale. It references removed/renamed files such as `usbdevice.cpp`, `usbdevice.h`, `usbcapture.cpp`, and `usbcapture.h`, while the current source uses `UsbDeviceBase`, `UsbDeviceLibUsb`, and `UsbDeviceWinUsb`.
- CMake builds with Qt 6. The qmake file has Qt 4/5-era structure and should be treated as historical unless it is updated.

## Main Runtime Shape

- `main.cpp` installs a Qt message handler, parses `--debug`, sets app metadata, creates `QtLogger`, constructs `MainWindow`, and starts the Qt event loop.
- `MainWindow` owns nearly all top-level objects: configuration, dialogs, USB device backend, player-control thread, timers, status labels, and capture metadata buffers.
- `Configuration` persists settings to `DomesdayDuplicator.ini` under `QStandardPaths::ConfigLocation`.
- `UsbDeviceBase` owns the high-throughput capture pipeline, common conversion/verification/statistics code, file writing, buffer sampling, memory locking, and process/thread priority helpers.
- `UsbDeviceLibUsb` implements Linux/macOS-style LibUSB enumeration, control transfer, and async bulk transfer.
- `UsbDeviceWinUsb` implements the Windows USB backend and is selected on Windows when `useWinUsb` is enabled.
- `PlayerCommunication` is a blocking serial protocol wrapper around Pioneer Level III LaserDisc player commands.
- `PlayerControl` is a `QThread` that polls player state, queues serial commands, and runs the automatic-capture state machine.
- UI layout is mostly Qt Designer `.ui` files. The main hand-written UI logic lives in `mainwindow.cpp`.

## Capture Flow

1. `MainWindow::StartCapture()` builds the output path from the configured capture directory and `AdvancedNamingDialog`.
2. It selects file extension by capture format:
   - `tenBitPacked` -> `.lds`
   - `sixteenBitSigned` -> `.raw`
   - `tenBitCdPacked` -> `.cds`
3. It maps GUI/configuration settings into `UsbDeviceBase::CaptureFormat` and queue sizes.
4. It calls `UsbDeviceBase::StartCapture(...)`.
5. `UsbDeviceBase::StartCapture()` connects to the selected device, opens the output file, allocates disk buffers, resets counters, and detaches a capture thread.
6. `UsbDeviceBase::CaptureThread()` allocates conversion buffers, locks memory, raises process priority where possible, and runs USB-transfer and processing worker threads.
7. The GUI polls transfer stats every 100 ms via `updateCaptureStatus()`.
8. `StopCapture()` asks the USB pipeline to stop in a detached thread so the UI remains responsive.
9. On completion, `updateCaptureStatus()` writes a sidecar JSON metadata file next to the capture output.

## Metadata Written After Capture

The sidecar JSON contains:

- `serialInfo`: player model/version, disc type/status, optional user codes, min/max timecode/frame/physical position.
- `namingInfo`: values collected from advanced naming, including title, disc type, format, audio type, side, mint marks, notes, and metadata notes.
- `captureInfo`: transfer result, duration, transfer/buffer/file/sample counts, sample min/max/clipping, sequence marker flag, and UTC creation timestamp.
- `timeSampledData`: time-indexed player timecode/frame, physical position, and status records.

The metadata writer is embedded in `MainWindow::updateCaptureStatus()` and is currently a large block of UI-adjacent logic.

## Configuration Defaults

`Configuration::setDefault()` currently sets:

- capture directory: user home directory
- capture format: 10-bit packed
- USB VID/PID: `0x1D50` / `0x603B`
- disk buffer queue size: 256 MiB
- small USB transfer queue: disabled
- small USB transfers: enabled
- Windows: WinUSB and async file I/O enabled
- non-Windows: WinUSB and async file I/O disabled
- serial speed: auto-detect
- advanced/per-side/amplitude/advanced stats UI options: disabled

`SETTINGSVERSION` is currently `4`. Increment it for incompatible settings changes.

## Areas To Treat Carefully

- Threading is central. The GUI thread, detached `std::thread`s, LibUSB callbacks, USB processing thread, USB transfer thread, and `PlayerControl` `QThread` all interact.
- `MainWindow::StopCapture()` detaches a lambda capturing by reference. Future changes should avoid destroying or replacing referenced objects while that detached stop thread can still be running.
- `UsbDeviceBase::StartCapture()` and `CaptureThread()` also use detached threads. Lifetime is guarded by wait/flag logic, not by RAII thread ownership.
- `PlayerControl` command queues are written by the GUI thread and read by the worker thread without a visible mutex around `commandQueue`, `parameterQueue`, or `stringParameterQueue`. That is a likely data-race hotspot.
- `PlayerControl` has non-atomic control fields such as `abort`, `reconnect`, and automatic-capture state read/written across threads. Some access is under `QMutex`, but not all.
- `MainWindow::updateAmplitudeUI()` repeatedly calls old-style `connect()` when settings change. Qt allows duplicate connections unless a unique connection is requested, so toggling settings may create duplicate timer slot invocations.
- The player polling loop sleeps for only 100 microseconds while connected, but serial commands are blocking and can have multi-second timeouts. Be cautious when adjusting polling frequency or adding more serial queries.
- Capture stats and metadata collection are stored in memory for the duration of capture. The code reserves up to 24 hours of 100 ms player samples, which is intentional but large.
- The app uses `std::filesystem::path((char8_t const*)QString::toUtf8().data())` in `MainWindow::StartCapture()`. That relies on temporary `QByteArray` lifetime within the full expression and should be revisited carefully if path handling changes.
- Some UI state restoration/error paths are duplicated. Failed start and completed capture both manually reset capture button text/style and menu enabled states.

## Known Incomplete Or Suspicious Items

- `PlayerCommunication::setStopFrame()` and `setStopTimeCode()` log that they are not implemented, but `PlayerControl` and automatic capture expose stop-frame/timecode plumbing.
- Remote button `rbRepeat` logs "not implemented".
- `DomesdayDuplicator.pro` is inconsistent with current source names.
- The CMake app version is `1.0`, while `main.cpp` sets the Qt application version to `2.1`.
- `UsbDeviceLibUsb::GetAllDomesdayDevices()` has a trace log string with a `{0}` placeholder but no argument.
- `MainWindow::playerDisconnectedSignalHandler()` logs the connected handler name, likely a copy/paste typo.
- `MainWindow::updatePlayerControlInformation()` records player status changes even outside a capture. If that is intentional, it should be documented; if not, the status buffer can grow during normal idle use until the next capture reset.
- `statusRecordObj[...] = entry.playerState` stores enum numeric values in JSON. That may be fine for tools, but string values would be more stable and human-readable.
- `physicalPositionRecordObj[...] = static_cast<qint64>(entry.physicalPosition)` truncates a `float` physical position even though the UI displays two decimal places.
- The sidecar metadata write converts `jsonString.toStdString()` twice when writing. It is harmless but unnecessary.

## Refactoring Opportunities

- Split `MainWindow` responsibilities:
  - capture orchestration
  - metadata building/writing
  - player status sampling
  - UI state transitions
- Introduce an RAII capture-session object to own capture timing, output paths, sampled metadata, and finalization.
- Replace detached threads with joinable ownership or explicit worker objects where possible.
- Protect `PlayerControl` command queues and automatic-capture state with a consistent mutex or move to queued Qt signal/slot calls.
- Move capture metadata serialization into a small pure-ish helper that can be unit-tested without hardware or Qt widgets.
- Create string conversion helpers for capture format, transfer result, player state, disc type, audio type, and serial speed to avoid duplicated enum mapping.
- Update or remove the qmake project file so future contributors do not chase a broken build path.
- Consider using `Qt::UniqueConnection` or explicitly disconnecting before reconnecting in `updateAmplitudeUI()`.
- Normalize versioning between CMake project metadata and `QCoreApplication::setApplicationVersion`.

## Test Strategy For Future Work

There are no apparent unit tests in this tree. Hardware-dependent behavior makes end-to-end testing hard, but useful coverage can still be added around:

- configuration version/default/read/write behavior using a temporary settings path
- enum/string conversion helpers
- capture filename generation from `AdvancedNamingDialog`
- metadata JSON generation from a synthetic capture-session record
- sample conversion and sequence-marker handling in `UsbDeviceBase`, if separable from the live capture object
- player serial response parsing with a fake serial transport

For manual verification, use at least:

- Linux/LibUSB device detection and capture start/stop
- Windows WinUSB path if it is still supported
- test mode capture
- each capture format extension/output mode
- capture failure paths: no device, bad output directory, USB disconnect during capture
- player-connected and player-disconnected flows
- automatic capture start/cancel/complete
- advanced naming plus metadata sidecar generation

## Suggested First Tasks

1. Decide whether CMake is the only supported build system. If yes, remove or clearly mark `DomesdayDuplicator.pro` as obsolete.
2. Fix low-risk obvious bugs: version mismatch, typo log strings, LibUSB trace placeholder, metadata physical-position truncation.
3. Add a metadata-builder helper with tests. This gives future work a safer landing zone without needing hardware.
4. Audit `PlayerControl` threading and protect command queues/state before expanding automatic capture or remote-control behavior.
5. Replace duplicate amplitude timer connections with unique or centralized connection management.

