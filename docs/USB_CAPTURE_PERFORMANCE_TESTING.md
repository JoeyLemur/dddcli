# USB Capture Performance Testing

Use this checklist to validate the conservative USB capture performance recommendations from the review. These tests focus on Linux/libusb capture hosts and are meant to answer three questions:

- Is the binary built with optimization appropriate for capture?
- Does the host sustain the expected capture rate without USB or storage backpressure?
- Which hot path should be measured before any code change?

These commands write large capture files. Use fast local scratch storage with enough free space, and keep the JSON sidecars from every run.

## 1. Optimized Build Baseline

Build a release-style binary first. A plain CMake build can be unoptimized on single-config generators, so use `RelWithDebInfo` or `Release` for performance testing.

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-perf
ctest --test-dir build-perf --output-on-failure
```

Record:

- `cmake --build build-perf` output
- `ctest` result
- compiler and kernel versions:

```sh
c++ --version
uname -a
```

Optional comparison: repeat the same capture tests with the default `build/` directory if you want to quantify the cost of an unoptimized build. Do not use the slower build for acceptance unless it is intentionally your deployment build.

## 2. Host Preflight

Confirm the DdD device is visible and negotiated as USB 3 SuperSpeed.

```sh
./build-perf/dddcli list-devices
cat /sys/bus/usb/devices/<device-suffix>/speed
cat /sys/bus/usb/devices/<device-suffix>/version
```

Expected:

- device path is reported by `list-devices`
- speed is `5000`
- version is `3.00` or newer

Confirm kernel USBFS and locked-memory headroom.

```sh
cat /sys/module/usbcore/parameters/usbfs_memory_mb
ulimit -l
```

Expected:

- `usbfs_memory_mb` is at least `512`
- `ulimit -l` is at least `524288` KiB, or `unlimited`

Record storage and filesystem details for the output path:

```sh
df -h /tmp
findmnt -T /tmp
```

Use the real output directory instead of `/tmp` if captures are written elsewhere.

## 3. Baseline Capture Rate

Run a short manual capture in the default LDS format.

```sh
./build-perf/dddcli capture \
  --duration 5 \
  --output /tmp/dddcli-perf-baseline.lds \
  --json /tmp/dddcli-perf-baseline.json
```

Expected:

- capture exits successfully
- JSON `captureInfo.transferResultString` is `success`
- output file is non-empty
- approximate sustained rate is near the expected DdD data rate

Calculate the output write rate from JSON `durationInMilliseconds` and `fileSizeWrittenInBytes`. For LDS, expect the written file rate to be lower than the raw USB input rate because samples are packed.

Also record:

- stderr progress output
- JSON `transferCount`
- JSON `sampleCount`
- JSON clipping fields
- output filesystem free space before and after

If the result is `USB memory limit` or libusb reports no memory, repeat only after fixing host tuning. Use the small transfer queue only as a fallback test:

```sh
./build-perf/dddcli capture \
  --duration 5 \
  --small-usb-transfer-queue \
  --output /tmp/dddcli-perf-smallq.lds \
  --json /tmp/dddcli-perf-smallq.json
```

## 4. Longer Stability Run

Run a longer capture to expose scheduler, memory pressure, and storage latency problems that a 5 second run can miss.

```sh
./build-perf/dddcli capture \
  --duration 120 \
  --output /tmp/dddcli-perf-120s.lds \
  --json /tmp/dddcli-perf-120s.json
```

Expected:

- transfer result is `success`
- rate is consistent with the 5 second baseline
- no sequence mismatch, USB transfer failure, or file write error
- no large unexpected drop in progress rate over time

Repeat once while the desktop is idle and once while normal background services are running. Avoid artificial stress tools for acceptance; they are useful only for margin testing.

## 5. Format Matrix

Run the same duration for each capture format. This helps separate USB ingest from CPU conversion and output write rate.

```sh
./build-perf/dddcli capture --duration 30 --format lds --output /tmp/dddcli-format.lds --json /tmp/dddcli-format-lds.json
./build-perf/dddcli capture --duration 30 --format raw --output /tmp/dddcli-format.raw --json /tmp/dddcli-format-raw.json
./build-perf/dddcli capture --duration 30 --format cds --output /tmp/dddcli-format.cds --json /tmp/dddcli-format-cds.json
```

Compare:

- transfer result
- file size and write rate
- sample count
- CPU usage observed during each run
- whether one format is more likely to fail or slow down

Interpretation:

- `raw` writes more bytes and stresses storage more.
- `lds` is the default path and exercises sequence stripping plus 10-bit packing.
- `cds` exercises decimation and should write much less data.

## 6. Queue And Transfer Settings

Test the current defaults first, then compare the fallback queue and large transfer mode.

```sh
./build-perf/dddcli capture --duration 30 --output /tmp/dddcli-defaultq.lds --json /tmp/dddcli-defaultq.json
./build-perf/dddcli capture --duration 30 --small-usb-transfer-queue --output /tmp/dddcli-smallq.lds --json /tmp/dddcli-smallq.json
./build-perf/dddcli capture --duration 30 --large-usb-transfers --output /tmp/dddcli-large-transfers.lds --json /tmp/dddcli-large-transfers.json
```

Expected:

- default queue succeeds on a tuned host
- small queue is a fallback, not the preferred performance configuration
- large transfers should not be accepted as better unless repeated runs show equal or better stability

Record any `USB memory limit`, `LIBUSB_ERROR_NO_MEM`, sequence mismatch, or transfer failure. These results are more useful than a single pass/fail because they point to kernel USBFS limits, host memory locking, or ring-buffer backpressure.

## 7. Storage Sensitivity

Repeat the baseline capture on each candidate output device.

```sh
./build-perf/dddcli capture \
  --duration 60 \
  --output /path/to/output/dddcli-storage-test.lds \
  --json /path/to/output/dddcli-storage-test.json
```

Compare local SSD, external USB storage, network storage, and any intended long-term capture volume separately. Avoid sharing the same USB controller between the DdD hardware and an external capture disk when possible.

Expected:

- local fast storage should be the reference result
- slower storage should not produce file write errors or visible rate drops
- storage that cannot sustain the rate should be rejected for real captures

## 8. Current Observability Gaps

The current CLI JSON sidecar is enough for coarse throughput checks, but it does not yet expose the exact hot-path timings from the review.

Before changing capture behavior, add temporary or permanent metrics for:

- time spent waiting for a disk buffer inside the libusb callback
- per-buffer sequence and metrics processing time
- per-buffer format conversion time
- per-buffer file write time
- high-water mark for filled disk buffers
- transfer resubmission failures grouped by libusb error

Acceptance for instrumentation:

- metrics must be cheap enough to leave enabled for capture testing
- status/progress/errors stay on stderr
- command outputs and JSON remain stable unless the schema change is intentional and documented
- quiet mode must not hide critical capture preflight failures

## 9. Results Template

Keep one block like this for every run:

```text
Command:
Build type:
Git revision:
Kernel:
CPU:
Output storage:
USB device path:
USB speed/version:
usbfs_memory_mb:
ulimit -l:
Duration requested:
JSON durationInMilliseconds:
JSON fileSizeWrittenInBytes:
Approx write rate:
JSON transferResultString:
JSON transferCount:
JSON sampleCount:
Format:
Queue options:
Observed CPU usage:
stderr notes:
Pass/fail:
```

## 10. Conservative Acceptance Criteria

Treat the capture path as healthy for normal use only when:

- optimized build passes software tests
- USB 3 speed and host tuning match the preflight expectations
- default LDS capture succeeds for 5 seconds and 120 seconds
- format matrix succeeds without sequence mismatch or file write errors
- intended storage sustains the write rate with margin
- repeated runs produce consistent file size, sample count, and transfer result

Any failure in these criteria should be investigated before changing the capture algorithm. The first fixes should usually be host tuning, optimized build configuration, storage choice, or added metrics.
