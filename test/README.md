# Peripheral regression and board tests

Run from the repository root. If `pio` is unavailable use
`"$HOME/.platformio/penv/bin/pio"`. The production environment remains the default build and ignores all tests;
select a named test environment explicitly.
Classes under `lib/board/` are shared by production and tests. Each `test_*`
folder is a separate Unity program; `src/main.cpp` is excluded from test builds.

## Native regression tests

```sh
pio test -e native --json-output-path .pio/native-results.json --junit-output-path .pio/native-results.xml
```

The native environment requires the system C/C++ compiler. It runs actual board
class sources and pinned LVGL 9.6.0 with fakes in `support/native/` for Arduino,
GPIO/time, LCD/PSRAM allocation, Wire, SPI and SD. These tests cover software
contracts, rendering, malformed input and injected failures, not electrical
behavior or DMA/PSRAM timing. Unity does not provide the fakes itself.

## Compile every hardware suite without touching the board

```sh
pio test -e hardware -e hardware-guided -e hardware-soak -e hardware-card-absent -e hardware-card-unsupported --without-uploading --without-testing
```

## Run on the ESP32 display board

Select the intended CH340C port explicitly for **both** upload and test output.
Close other serial monitors first. Every embedded `pio test` command flashes
special test firmware and collects Unity results over UART0 at 115200 baud.
The test program waits two seconds for serial connection before starting.

```sh
pio test -e hardware --upload-port <port> --test-port <port> --json-output-path .pio/hardware-results.json --junit-output-path .pio/hardware-results.xml
pio test -e hardware-guided --upload-port <port> --test-port <port> --json-output-path .pio/guided-results.json --junit-output-path .pio/guided-results.xml
pio test -e hardware-soak --upload-port <port> --test-port <port> --json-output-path .pio/soak-results.json --junit-output-path .pio/soak-results.xml
```

`hardware` runs three independent suites: display rendering/scanout, GT911
identification and polling, and SD write/read/remove while display and touch run.
Use `--filter 'embedded/test_touch'` (for example) to select just one suite.
Insert a known FAT16/FAT32 card **before boot**. No suite formats a card.

`hardware-guided` prompts on screen and serial for four inset corners and center,
a left-to-right held drag, and a two-finger sequence. Each wait is bounded at
30 seconds. Keep the first finger still, add a second, lift only the second,
then lift the first. Release between target taps. Inactivity or incomplete
sequences are Unity **ignored** results, not successful hardware verification.
Wrong-position taps that never reach the target also leave that check incomplete.
An interrupted drag can be retried within the same 30-second window and its
count is logged. A started drag that never completes continuously fails.
Actual protocol errors or tracked-contact jumps fail. Release logs distinguish
empty reports, missing contact IDs, I2C/report errors and the 150 ms stale timeout.

`hardware-soak` runs for one minute. It animates, polls touch and verifies an
8192-byte binary file about once per second. Each file operation handles 512-byte
chunks with LVGL service between chunks. It reports frame rate, internal memory,
largest allocation block, PSRAM free, SD cycles and touch errors every five seconds.
Frame callbacks must remain between 25 and 33 Hz, a broad tolerance around the
28.86 Hz nominal baseline. At least 50 file cycles must complete. Memory is
reported for trend assessment, not checked against a fragile fixed allocation
count. A reset prevents the current test from completing normally; inspect the
raw log for boot/reset traces rather than accepting a later restarted run.

Each SD verification checks that a candidate `/pio-test-<time>-<suffix>.bin` path
does not exist before opening it. Only the selected test file is written/deleted.
Files are closed and owned temporary files are removed in `tearDown`, including
assertion failures. Cleanup errors print the retained pathname and fail the test.
If power is lost, an orphan file can remain; later runs choose a different path.
Close application file handles before calling `SdCard::end()`.

## Card failure fixtures

Power off before swapping cards; there is no card-detect/hot-plug service.

```sh
# Power off, remove card, then power on:
pio test -e hardware-card-absent --upload-port <port> --test-port <port>
# Power off, insert a known unsupported filesystem, then power on:
pio test -e hardware-card-unsupported --upload-port <port> --test-port <port>
```

Both require a failed mount while display frames and GT911 polling continue.
The operator must identify the fixture: a mount failure alone cannot distinguish
an unsupported filesystem from a missing/broken card. Neither suite formats or
writes to the card. Physically tapping to confirm UI behavior is an additional
manual observation.

## Visual results and restoration

Frame interrupts and successful copies cannot prove correct LCD appearance.
For display and soak tests, inspect swatch order, gradients, corner labels,
perimeter/final row and column, and moving animation. Record drift, tearing,
flicker, glare limitations and your explicit visual verdict alongside the Unity
reports. Guided touch coordinates do not substitute for visual display checks.
Automatic display tests turn off the backlight during cleanup after five seconds;
the soak offers the longer viewing opportunity.

Restore the production portrait reader after the session:

```sh
pio run -e esp32-s3-devkitc-1 -t upload --upload-port <port>
pio device monitor -e esp32-s3-devkitc-1 --port <port> -b 115200
```

Record firmware build identity, port, board revision/power source when known,
card/filesystem, test reports and manual observations in `docs/hardware.md`.
A compiled suite is not a hardware pass, and ignored tests remain pending.

## Portrait reader tests

See [reader documentation](../docs/reader.md) for implementation and controls.
Native tests additionally run real pinned ZIP/JPEG/PNG codecs, ownership and
partial-cache cleanup, natural ordering, malformed/CRC failures, 4096-wide PNG,
image fitting, portrait flush pixels/bounds, single touch rotation, statistics
recovery, and multiple-book resume. Synthetic CBZ fixtures live in `fixtures/`.

```sh
pio test -e hardware-reader -e hardware-reader-guided -e hardware-reader-benchmark --without-uploading --without-testing
pio test -e hardware-reader -v --upload-port <port> --test-port <port> --json-output-path .pio/reader-hardware.json --junit-output-path .pio/reader-hardware.xml
pio test -e hardware-reader-guided -v --upload-port <port> --test-port <port>
pio test -e hardware-reader-benchmark -v --upload-port <port> --test-port <port>
```

`hardware-reader` checks generated comics, cancellation, JPEG/PNG pixel colors,
multiple books, application restart/resume and a one-minute page-turn workload.
The complete suite takes longer than one minute because setup/extraction and
resume checks precede the soak. `-v` displays serial health/progress telemetry;
without it PlatformIO can remain quiet until a Unity assertion or final result.
Do not interrupt a healthy run solely because it is quiet. Its uniquely named
fixture folder is deleted on cleanup; an interrupted run may leave it behind.

`hardware-reader-guided` verifies five displayed portrait targets with 30-second
press/release windows. `hardware-reader-benchmark` exercises 1/4/8/16 MHz SD with
portrait scanout, animation and touch, 20 seconds per speed. Individual faster
clock failures are logged as rejected settings; 1 MHz failure fails the suite.
A software pass requires a separate visual verdict before selecting a clock.
The one-minute reader run then verifies the selected frequency in real decoding.
Physical power-cycle resume and manual browser/page/overlay/Cancel controls
remain separate checks from queued integration commands.

`hardware-reader-samples` is an opt-in provisioner for manual validation. It
verifies ownership/content before reusing `/Reader samples/`, writes only missing
synthetic sample files, and intentionally retains them for physical reboot tests.
Its live loop runs the reader after Unity completes. Restore normal production
firmware before final manual/power-cycle checks. It can also remove abandoned
`/reader-test-*` folders only after confirming every entry and all bytes match the
two generated fixture books; unknown or partial contents remain untouched.
