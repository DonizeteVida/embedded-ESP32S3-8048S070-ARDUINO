# Portrait CBZ reader

The firmware boots into the SD filesystem browser at `/`. Turn the board
clockwise: the reader is 480×800. RGB timing remains 16 MHz. Insert a FAT16/FAT32
card before powering on; there is no hot-plug detection or formatting service.

Folders and all file types remain visible. Up navigates to the parent; Refresh
rescans. Previous/Next show ten entries per directory page using the same ten
LVGL row objects. Only the reserved `/.comic-reader` application directory is
hidden. Selecting CBR explains that support is coming later; other unsupported
files show a message. CBZ rows show saved page progress when available.

Tap a CBZ to extract all supported JPEG/PNG entries. Cancel removes partial
cache files. When a page is visible, tap the left/right third to move one page,
or the center to toggle the overlay. Back returns to the folder. Stats shows
page count, opening count, accumulated reading time and completion. The
whole image fits the display with black margins; there is no cropping, zoom,
spread mode or automatic page turn. The overlay can cover part of the image
until hidden.

## Application structure

- `src/main.cpp`: Arduino initialization and loop delegation only.
- `lib/reader/DirectoryBrowser`: bounded directory scanning and pagination.
- `lib/reader/ComicExtractor`: ZIP validation, generated cache filenames, CRC
  checking, cancellation, natural archive-path ordering, and cache lifecycle.
- `lib/reader/PageDecoder`, `JpegAdapter`, `PngAdapter`: incremental image
  decoding into a staging RGB565 PSRAM buffer, then publication on success.
- `lib/reader/ReadingStatsStore`: versioned, checksummed SD records and recovery.
- `lib/reader/ReaderApp`: queued actions, service loop, sessions and reading time.
- `lib/ui/ReaderScreen`: LVGL widgets, recycled file rows and event dispatch.
- `lib/board/`: shared production/test peripheral implementations.

LVGL, I2C, filesystem jobs and decoding all execute on the Arduino task. Events
only queue commands. Extraction advances in bounded 4096-byte chunks; decoder
callbacks and ZIP preflight pump LVGL and yield. Filesystem operations themselves
are synchronous, so a slow or failing card can still delay an individual call.
No full-resolution image allocation is made. The displayed and staging images
use 768,000 bytes each in PSRAM, plus a bounded page index and codec workspace.

## Accepted files and limits

CBZ is ZIP with stored or DEFLATE entries. Baseline JPEG and non-interlaced PNG
(up to eight bits per color channel, as supported by PNGdec) are supported up to
4096×8192 source pixels. PNG CRC checking is enabled. Natural
ordering applies to complete archive paths, so `2.jpg` precedes `10.jpg`.
Directories, Apple `__MACOSX`/`._` metadata and non-image members are ignored.

The reader rejects encrypted, split, ZIP64, linked and unsupported-compression
entries, unsafe or oversized paths, ZIP CRC failures, archives over the signed
32-bit codec API limit, more than 4096 pages, pages over 64 MiB, and more than
1 GiB extracted data. It requires an additional 1 MiB free-space margin. Corrupt,
progressive-JPEG, interlaced-PNG and oversized pages display an error without
advancing saved progress. Dismiss the error and tap right to skip a bad page.

Dependencies are pinned: LVGL 9.6.0, unzipLIB 1.0.0, JPEGDEC 1.8.4, and PNGdec
commit `64f9f3bc11cc1acb49f5bce823baae6ece910ba6` (upstream tag `1.1.7`, whose
package metadata still says `1.1.6`). `scripts/codec_build.py` isolates PNGdec's
bundled zlib symbols from unzipLIB's bundled inflater. Codec headers are included
only in their separate adapters. `PNG_MAX_BUFFERED_PIXELS=32770` is applied to
both PNGdec and its adapter, allowing two 4096-pixel RGBA scanlines plus filters.

## Cache and progress

`/.comic-reader/owner` identifies reader-owned storage. An existing directory
with an unknown marker is refused. On every open, the previous session is saved,
files and images are closed, and only `/.comic-reader/cache/` is cleared. Archive
paths never become output paths: cache pages use generated numeric names.
Unexpected subdirectories inside the flat private cache stop cleanup.

Statistics write failures display a warning; a failed panel copy never advances
saved progress.

Progress is independent under `/.comic-reader/stats/`. Identity combines the
canonical SD path with a fingerprint of archive entry names, CRCs and sizes.
Renamed/replaced books therefore have different records. This fingerprint is
for identity and accidental changes, not authentication. Directory progress
uses a path index and file size; opening computes the full metadata fingerprint.
A same-size replacement can briefly show old progress in the browser before
being opened, but it resumes from its own archive identity.

Records contain last successfully displayed page (zero-based internally), total
pages, completion, opening count, and accumulated milliseconds with a page
visible. They save after page changes, every 30 seconds, and on Back/exit.
Loading and browsing time is excluded. Completion is reached when the final
page is displayed successfully. Every successful reopen increments the opening
count and re-extracts the archive before restoring its saved page. Boot always
opens the browser. A sudden power loss can lose up to the last checkpoint's
reading time. Alternating `.json`/`.bak` records and a verified `.tmp` permit
recovery after interrupted writes/renames; they cannot guarantee survival of
arbitrary card/controller corruption.

The partition table is unchanged: persistence lives entirely on SD. Cache
cleanup never deletes statistics. Do not put personal files in the application's
reserved directory. Removing it deliberately resets cache and all reading stats.

## Build and tests

```sh
pio run -e esp32-s3-devkitc-1
pio test -e native
pio test -e hardware-reader --upload-port <port> --test-port <port>
pio test -e hardware-reader-guided --upload-port <port> --test-port <port>
pio test -e hardware-reader-benchmark --upload-port <port> --test-port <port>
```

The reader hardware suite creates uniquely named synthetic CBZ books in a new
`/reader-test-<time>-<suffix>/` folder and removes them in fixture cleanup. Its
statistics remain in private storage as test records. It checks extraction,
JPEG/PNG pixels, multiple books, cancellation, reopening, application restart and
one minute of page turns with stats writes. Application restart is distinct from
physical power-cycle verification. Native tests use the pinned codecs and LVGL
with fake hardware. See [test guidance](../test/README.md) for all environments,
JSON/JUnit export, physical fixtures and restoring the production reader.

The guided suite requests five portrait corner/center taps; unfinished actions
are ignored, not passed. The clock benchmark exercises 1, 4, 8 and 16 MHz SD with
portrait gradients/animation, bounded binary write/read verification, LVGL/touch
polling and memory telemetry for 20 seconds per speed. Select a faster default
in `include/reader_config.h` only after both integrity and visual/touch checks.
RGB stays at 16 MHz for every test. Hardware results and outstanding manual
checks belong in [the hardware guide](hardware.md).

Fixtures in `test/fixtures/` are generated synthetic art. Regenerate with
`python3 scripts/generate_reader_fixtures.py` using Pillow. They cover nested and
numbered JPEG/PNG pages, Apple metadata, CRC errors, unsafe paths, unsupported
archive flags/methods, progressive JPEG and PNG limits. They contain no commercial
comic pages.

A dedicated `hardware-reader-samples` suite provisions the generated manual-test
books under `/Reader samples/` and leaves the reader running. It intentionally
retains that marked folder for physical power-cycle and multi-book checks. It
refuses unknown folders or changed existing sample files. `Long book.cbz` offers
a 100-page extraction so Cancel can be exercised. The other samples cover natural
page order, a second book, a corrupt page, and the deferred CBR message. This
provisioner is opt-in and is never called by normal firmware.
