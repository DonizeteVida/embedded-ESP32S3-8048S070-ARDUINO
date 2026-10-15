# ESP32-8048S070 hardware reference

Evidence reviewed: 2026-10-15. This guide covers the whole board and the current
PlatformIO firmware. Essential settings are recorded here so the vendor download
is not required just to understand the project.

## Evidence and current status

| Status | Meaning in this guide |
| --- | --- |
| Project-configured | Read from this repository or the installed SDK; not a physical measurement. |
| User-confirmed | The user reported rendering with the earlier demonstration. |
| Hardware-observed | The current board was flashed and inspected using serial telemetry and camera captures; see the validation section. |
| Vendor-documented | Shown in the supplied specifications, schematics, datasheets, or examples; actual board population may differ. |
| Unverified | Requires checking the physical board or running a hardware test. |

The physical PCB revision, exact module ordering code, and other fitted
peripherals are **unverified**. The touch controller identifies as GT911 over I2C
at 0x5D with an 800 x 480 configured coordinate range; real tap/release events
and tap-button clicks were observed. Rendering with the earlier 25 MHz demo is
user-confirmed. The current LVGL firmware has been flashed and inspected on
the board. Visible row drift appeared at 20/25/33 MHz; the current target is
16 MHz. The layout and animation can be assessed through camera captures,
but reflection and the oblique camera angle limit precise color/contrast
assessment. Touch detection, tap/release events, and button clicks are hardware-observed;
full corner/drag alignment checks remain open. SD, audio, and wireless remain unverified.

Vendor setup instructions are reference material, not instructions for this
repository. The existing PlatformIO configuration remains the software baseline.
Generic examples and controller support in a library do not establish which
components are fitted to this board.

## Board family, power, and mechanics

The supplied [board specification][spec] (pp. 3-4) describes an
ESP32-S3-WROOM-1-based 7-inch TFT board with 800 x 480 resolution, 16-bit RGB
color, 16 MB flash, and 8 MB PSRAM. It lists two product variants:

| Vendor SKU | Touch option |
| --- | --- |
| ESP32-8048S070N | No touch |
| ESP32-8048S070C | Capacitive touch |

The specification lists EK9716 as the display driver chip. The project drives
the RGB interface using `esp_lcd`; it does not configure an SPI display driver.
The schematic also includes a resistive-touch alternative, whose presence on
the user's board is unverified.

The supplied [Espressif module datasheet][module] (pp. 2-3, 10-13) identifies a
dual-core Xtensa LX7 MCU up to 240 MHz, 512 KB SRAM, 2.4 GHz 802.11 b/g/n Wi-Fi,
and Bluetooth 5 LE. The getting-started manual's Classic Bluetooth/BLE 4.2
description conflicts with this ESP32-S3 datasheet; use the chip/module
capabilities when planning wireless features. N16R8 is a module option consistent
with the advertised memory, but the installed module marking has not been read.

| Item | Vendor evidence and practical interpretation |
| --- | --- |
| Board supply | 5 V; approximately 500 mA consumption in specification p. 4. This is an approximate operating figure, not a maximum or a measured project requirement. |
| Supply inputs | USB-C VBUS or P1 VIN. MCU schematic upper-left shows VIN through AO3401 Q1 to the board `5V` net, and VBUS through Schottky D1 to `5V`. No wide VIN range is established. |
| Logic supply | Board regulation produces 3.3 V. Expansion signal pins are ESP32 GPIO/UART nets; do not interpret the 5 V board input as their logic voltage. |
| Audio supply | LCM schematics tie amplifier VDD and its SD_MD path to **VBUS**, distinct from the board `5V` net. Audio with VIN-only power is unverified. |
| Board outline | 181 x 108 mm, per specification p. 4 and mechanical drawings. |
| Mounting labels | Mechanical drawings show 165 x 100 mm center spacing and R1.6 holes (3.2 mm diameter). Offset annotations do not reconcile clearly with the outline; measure before designing an enclosure. |

Sources: [MCU V1.1 schematic][mcu11], upper-left VIN/Q1 and upper USB VBUS/D1 sections;
[LCM V1.1 schematic][lcm11], lower-center audio section; [dimensions drawing][dimensions].
Board dimensions and bare LCD-module dimensions are different quantities;
[JC8048B070N panel datasheet][panel] p. 3 describes the bare panel.

### Hardware revisions

The supplied [revision note][revision] states that V1.1 moves audio BCLK from
GPIO19 to GPIO0 and addresses programming failures on some computers. It does
not explain the precise circuit change responsible for the programming fix.

| Signal | V1.0 LCM drawing | V1.1 LCM drawing |
| --- | --- | --- |
| Audio DIN / ESP32 DOUT | GPIO17 | GPIO17 |
| Audio LRCLK | GPIO18 | GPIO18 |
| Audio BCLK | GPIO19 | GPIO0 |

Use both columns until the actual PCB marking is checked. The older MCU image's
filename says V1.0 while its title block says V0.1; retain that source ambiguity.

## Current display implementation

Sources: [firmware implementation](../src/main.cpp) (`display` namespace for
configuration and the LVGL adapter; `touch` for GT911 input; `app` for
the validation screen and diagnostics) and [LVGL configuration](../include/lv_conf.h). All application
implementation lives in `main.cpp`; LVGL configuration and the C assertion
bridge declaration remain in headers because LVGL compiles separately.
Arduino uses the native ESP-IDF `esp_lcd` RGB panel driver with LVGL 9.6.0
software rendering. Arduino_GFX and the historical vendor LVGL copy are not
firmware dependencies. The pin map is retained from the earlier rendering demo;
the current clock was selected by flashing and comparing camera recordings.

### RGB565 data and control pins

The firmware's B/G/R indices are low-to-high bits within each RGB565 component.
The raw panel uses its upper color bits; unused lower panel bits are grounded
in the LCM drawings. Project pins match the active vendor [HelloWorld example][hello]
(lines 18-24) and both LCM schematic LCD1 regions.

| RGB data bit | Component bit in firmware | ESP32 GPIO |
| --- | --- | --- |
| D0 | B0 | 15 |
| D1 | B1 | 7 |
| D2 | B2 | 6 |
| D3 | B3 | 5 |
| D4 | B4 | 4 |
| D5 | G0 | 9 |
| D6 | G1 | 46 |
| D7 | G2 | 3 |
| D8 | G3 | 8 |
| D9 | G4 | 16 |
| D10 | G5 | 1 |
| D11 | R0 | 14 |
| D12 | R1 | 21 |
| D13 | R2 | 47 |
| D14 | R3 | 48 |
| D15 | R4 | 45 |

| Control signal | ESP32 GPIO | Behavior/evidence |
| --- | --- | --- |
| HSYNC | 39 | Project configuration and LCM LCD1 wiring |
| VSYNC | 40 | Project configuration and LCM LCD1 wiring |
| DE | 41 | Project configuration and LCM LCD1 wiring |
| PCLK / DCLK | 42 | Project configuration and LCM LCD1 wiring |
| Backlight / BL_CTR | 2 | Project drives HIGH to enable; MCU drawing lower-right LED-driver enable |
| Separate DISP | Disabled, `disp_gpio_num = -1` | No display-enable API call; GPIO0 is not driven by this port. |

The raw LCD connector also carries panel supply voltages. It is not a general
expansion connector. Consult the panel specification for cable contact count;
schematic symbols include additional grounded pads.

### Timing, flags, and buffers

| Parameter | Current project value |
| --- | --- |
| Resolution / bus | 800 x 480 / 16 bits, RGB565 |
| Pixel clock | Requested 16,000,000 Hz, `LCD_CLK_SRC_PLL160M` |
| Horizontal pulse / back porch / front porch | 30 / 16 / 210 pixel clocks |
| Vertical pulse / back porch / front porch | 13 / 10 / 22 lines |
| HSYNC / VSYNC flags | `hsync_idle_low = true`, `vsync_idle_low = true` |
| DE flag | `de_idle_high = false` |
| Pixel-clock flags | `pclk_active_neg = true`, `pclk_idle_high = false`; clock data out on falling edge |
| Display control | Unused DISP explicitly disabled; backlight on GPIO2 |
| Refresh behavior | `relax_on_idle = false`, continuous refresh |
| Framebuffer placement | `fb_in_psram = true`, requests preferential allocation in PSRAM |
| SRAM / PSRAM transfer alignment | 8 / 64 bytes |
| Full RGB565 framebuffer | 800 x 480 x 2 = 768,000 bytes = 750 KiB, before driver overhead |
| LVGL draw buffer | One 64-byte-aligned, internal 800 x 48 x 2-byte buffer: 76,800 bytes (75 KiB) |
| LVGL allocator | Builtin 65,536-byte heap (64 KiB), included in static RAM usage |
| LVGL render mode / format | Partial / native RGB565, no byte swap or row padding |
| LVGL service / refresh | Arduino loop approximately every 5 ms; 16 ms LVGL refresh period; no LVGL OS integration |

Derived nominal panel refresh is `16,000,000 / 1056 / 525`, approximately
28.86 Hz. This is calculated from requested timing, not a measured physical
pixel clock. The serial frame-callback rate observes driver scanout events;
it is distinct from LVGL rendering FPS and does not measure image quality.

The [panel datasheet][panel], p. 7, lists a 29-38 MHz dot clock (33 MHz typical),
horizontal periods 1026-1086 (1056 typical), and vertical periods 515-535
(525 typical). Those are bare-panel timing figures, not a guarantee that the
ESP32/PSRAM scanout can supply pixels reliably at that rate. Actual camera tests
with this LVGL firmware showed row drift at 33, 25, and 20 MHz. Therefore the
board configuration uses 16 MHz, matching the vendor [HelloWorld example][hello]
(lines 27-35), despite being below the bare-panel clock range. This is an
observed operating choice for this board/software stack, not a claim of
datasheet-range compliance.

Pulse/porch values retain the panel's typical totals; falling-edge data output
leaves setup time for rising-edge panel sampling (pp. 7-9). The vendor
[7.1 music example][music] uses the same pin/timing pattern at 12 MHz.

### Clock validation on the connected board

All rows used the same pins, porch values, LVGL buffers, and driver; only PCLK
changed. The serial rate is a frame-interrupt count, not evidence of correct
pixel delivery. The earlier 25 MHz random-fill demo did not exercise the same
LVGL workload.

| Requested PCLK | Nominal refresh | Observed steady frame callbacks/s | Camera result |
| --- | --- | --- | --- |
| 33 MHz | 59.52 Hz | about 59.4 | Severe shifted/repeated content |
| 25 MHz | 45.09 Hz | about 45.0 | Shifted/repeated content |
| 20 MHz | 36.08 Hz | about 36.0 | Shifted/repeated content |
| 16 MHz | 28.86 Hz | median 28.79; range 28.76-29.00 | Clean layout in reviewed captures over the ten-minute run |

The pattern is consistent with bandwidth-limited RGB scanout. Espressif's
[LCD FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html)
describes insufficient PSRAM/GDMA bandwidth causing drift at high PCLK, and
reducing PCLK as a mitigation. This is a supported explanation, not a measured
bus-underflow trace. Higher rates or additional flash/Wi-Fi load require fresh
hardware validation; newer IDF bounce-buffer/XIP support is a possible future
upgrade, not something enabled by this IDF 4.4 port.

The camera recordings, serial logs, and firmware metadata used for these
observations are not included in this repository. The results below record
the completed test session; new hardware changes require fresh validation.

The final image was flashed and monitored for 610 seconds on 2026-10-15.
The saved serial data spans more than 605 seconds and contains 121 diagnostics.
Only the initial boot banner appeared: no unexpected reset, watchdog, assertion,
or display-copy failure was recorded. Steady frame callbacks ranged from
28.76 to 29.00/s, with median 28.79/s. Every memory sample reported 223,928 bytes
free internal RAM, a 184,308-byte largest internal block, and reported PSRAM
total/free 8,386,215 / 7,617,827 bytes. Start/mid/end camera clips and periodic
still checks showed an intact layout and moving progress bar without persistent
row drift. This is verification of the current validation-screen workload,
not continuous per-frame certification or a test with Wi-Fi/flash-write load.

### LVGL port and validation screen

`display::begin()` holds backlight LOW, checks PSRAM, creates/resets/initializes
the RGB panel, allocates the internal draw buffer, initializes LVGL, and registers
an 800 x 480 display. Allocation and SDK failures are reported over serial;
initialization failures release acquired resources. LVGL assertions also disable
backlight, print an error, and abort instead of silently hanging.

The main sketch creates six RGB/white/black/gray swatches, three channel gradients,
four corner labels, a one-pixel perimeter, uptime, and an endlessly reversing
progress bar. The touch-enabled screen adds a tap-count button, coordinates,
press/error counts, and an orange pointer marker. `display::show()` forces the initial LVGL render, then waits for a
scanout frame boundary before enabling backlight. Missing frame interrupts cause
a reported failure after 250 ms; the backlight stays off.

LVGL uses `millis()` for ticks; all UI calls run on the Arduino task. The flush
callback converts inclusive LVGL ends to exclusive SDK ends (`x2 + 1`, `y2 + 1`).
IDF 4.4.7 copies each rectangle into its own framebuffer and writes back PSRAM
cache synchronously, so `lv_display_flush_ready()` follows that call, even on
an error to avoid a stuck LVGL flush. The frame ISR only increments a protected
counter. No private framebuffer pointer, asynchronous pixel ownership, or
LVGL ISR calls are used. A single scanout framebuffer can exhibit tearing;
this implementation does not claim tear-free animation.

Every five seconds, serial diagnostics report frame callbacks per second,
free internal RAM, largest internal allocation block, and total/free PSRAM.
Static build RAM includes the 64 KiB LVGL heap; the 75 KiB draw buffer and
driver DMA/interrupt resources allocate at runtime. The 750 KiB RGB framebuffer
resides in PSRAM. Allow additional room for task stacks, driver overhead, and
future UI objects/layers rather than treating this as the entire memory budget.

### Corrections from the earlier demo

| Earlier issue | Current behavior |
| --- | --- |
| Omitted DISP defaulted to GPIO0 | `disp_gpio_num = -1`; unsupported display-enable call removed ([SDK header][rgb-header], line 102). |
| `DISPLAY_WIDTH - 1` excluded the last column | Inclusive LVGL ends convert to exclusive SDK ends; full-width draws end at 800 ([SDK contract][ops-header], lines 47-56). |
| `memset` repeated a random byte rather than a RGB565 color | Random scanline demo removed; LVGL supplies native RGB565 pixels. |
| 25 MHz / 928 clocks per line, later 33 MHz trial | Hardware-tested 16 MHz target / 1056 clocks per line, with vendor porch values; higher clocks showed drift. |

## Peripheral wiring

The wiring tables below are vendor-documented. The current GT911 detection
and configuration have also been checked on the connected board; the remaining
peripherals are untested in the current project.

### Capacitive touch / GT911 examples

| Signal | ESP32 GPIO / setting | Evidence |
| --- | --- | --- |
| SDA | 19 | LCM FPC1 contact 2; example `TOUCH_GT911_SDA` |
| SCL | 20 | LCM FPC1 contact 3; example `TOUCH_GT911_SCL` |
| Reset | 38 | LCM FPC1 contact 4; example `TOUCH_GT911_RST` |
| Interrupt | 18 through MCU R17, 0 ohm, to TP_INT | LCM FPC1 contact 5; vendor examples instead pass `INT = -1` and poll |
| Supply | 3.3 V / GND | FPC1 contacts 6 / 1; schematic also shows grounded symbol pads 7/8 |
| I2C pull-ups | 4.7 kohm to 3.3 V | LCM R3/R4 |
| Address | 7-bit 0x5D default; 0x14 alternative | Bundled GT911 headers; this board responded at 0x5D with product ID `911` |
| Example screen size | 800 x 480 | Widgets `touch.h` lines 19-29 |

Sources: [MCU V1.1][mcu11], Extended IO block R17 immediately above P3;
[LCM V1.1][lcm11], middle-right
capacitive section; [Widgets touch wrapper][touch], lines 19-29, 52-55, 115-168.
The examples' disabled IRQ does not mean the interrupt wire is absent. GPIO18
is also audio LRCLK; polling in software does not remove the physical sharing.
Check actual population and electrical behavior before combining those functions.

The bundled touch drivers are materially different:

| Driver | Initialization behavior and integration concern |
| --- | --- |
| `TAMC_GT911` in `Libraries/Gt911-arduino-main` | `begin()` resets via INT/RST, reads configuration, and writes resolution. Its unsigned INT argument turns `-1` into 255; reset code does not guard that sentinel. Do not copy this setup blindly. |
| `Touch_GT911` in `Libraries/Touch_GT911` | Music-example fork; `begin()` only sets the address and starts Wire. It does not perform the same reset/configuration sequence. |

See [TAMC implementation][tamc] lines 5-31 and [music touch implementation][touch-driver]
lines 5-29. Both drivers invert coordinates for their `ROTATION_NORMAL` setting;
wrappers also reverse mapping endpoints. Validate corner coordinates and axis
direction rather than assuming that the name implies unchanged coordinates.
Samples pass only the first touch point to LVGL, despite five-point driver arrays.

### Current GT911 implementation

The `touch` namespace in [main.cpp](../src/main.cpp) implements the register
protocol from the [music driver][touch-driver] and [TAMC driver][tamc], using
Arduino Wire directly; no additional library dependency is needed. The LVGL
adapter targets the pinned 9.6 API rather than the vendor LVGL 8 wrappers.

| Setting | Current behavior |
| --- | --- |
| Bus | Wire/I2C0, SDA19, SCL20, 100 kHz, 20 ms transaction timeout |
| Detection | Read product ID at 0x8140; accept `911`; try 0x5D then 0x14 |
| Reset | First try the already-running controller, as in the music example. If neither address identifies, hold INT18/RST38 LOW for 10 ms, raise RST, wait 5 ms, keep INT LOW for 50 ms, return INT to INPUT, wait 50 ms, and probe again. |
| Resolution | Read little-endian X/Y limits from 0x8048-0x804B; reject invalid limits. Do not overwrite the vendor configuration/checksum. |
| LVGL input | One `LV_INDEV_TYPE_POINTER`, attached to the current display; read timer 16 ms on the Arduino task, with no touch ISR |
| Reports | Read ready bit and contact count at 0x814E; accept up to five 8-byte contact records starting at 0x814F; acknowledge ready reports by writing zero to 0x814E |
| Selection | Follow one contact ID across reordered packets; release when that ID disappears. Another remaining finger may become the pointer on a subsequent report. No LVGL multi-touch gestures are exposed. |
| Coordinates | Scale valid raw indices 0..limit-1 into 0..799 / 0..479. `kSwapXY`, `kInvertX`, and `kInvertY` currently false; vendor driver inversion plus wrapper reverse mapping cancel out. |
| Errors/releases | Reject short transfers, failed acknowledgements, invalid counts/coordinates; release on errors. Retain the last state between reports but release after 150 ms without a fresh report. Log errors at a limited rate and recover on later valid reports. |
| Missing controller | Log the initialization failure and keep the display/demo usable without an input device |

Only ready-report acknowledgements are written during normal operation;
configuration and controller flash are left intact. Unlike the TAMC example,
`-1` is never converted to an unsigned GPIO number. The fallback reset drives
the real GPIO18; polling does **not** make that signal safe to share with audio.
SDA19/SCL20 also remain unavailable for native USB. Resistive touch and other
controller families are not implemented.

The report format was cross-checked against
[Espressif's GT911 driver](https://github.com/espressif/esp-bsp/blob/master/components/lcd_touch/esp_lcd_touch_gt911/esp_lcd_touch_gt911.c).
That is protocol reference material; the project keeps its Arduino/Wire workflow.

On 2026-10-15 the new firmware was built and uploaded through CH340C. Serial
identified `GT911 911 at 0x5d`, raw 800 x 480. The user touched the screen: serial
recorded seven down/up sequences and seven tap-button clicks; a camera capture
showed `Tap test: 7`, released state, seven presses, and zero errors. Full
corner alignment and drag-following are not yet verified. Reset fallback is
host-tested but was not needed on this boot. Serial sampling continued for
about two minutes with constant free-memory readings and no touch errors.
The host serial reader then reported a disconnect or competing reader; a
subsequent camera frame still showed the UI running at 135 seconds uptime
with seven clicks and zero errors. This was not a new ten-minute touch test.

A temporary host harness linked the actual `main.cpp` and LVGL 9.6 against
simulated Wire/Arduino/LCD functions. All 18 scenarios passed: the previous nine
display cases plus touch packet handling, real LVGL button press/release and
outside-button taps, alternate address, fallback reset, missing I2C bus,
missing controller, incorrect product ID, failed configuration read, and invalid
resolution. Packet checks covered all edges, reordered contacts, release and
stale state, oversized counts, invalid coordinates, short reads, I2C/ack failures,
and recovery. These tests do not measure electrical bus timing or physical
orientation. Build usage: 85,752 bytes static RAM; 673,049 bytes flash. The
existing display clock, memory allocation, partitions, and dependencies remain
unchanged.

### SPI microSD / TF

| Signal | ESP32 GPIO |
| --- | --- |
| CS | 10 |
| MOSI | 11 |
| SCK | 12 |
| MISO | 13 |
| Card detect | Schematic contact is not connected to a GPIO |

Sources: MCU schematic lower-left SD socket; [7.2 HAL definitions][sd-pins],
lines 11-14, and its [initialization][sd-init], lines 9-24. The example mounts
with `SD.begin(SD_CS, SPI, 1000000)` and handles mount failure/no card. Its main
sketch comments out a direct initialization call, so presence of that sketch
does not prove SD operation. This evidence describes SPI mode, not an SDMMC pinout.

### Current SD implementation

The `sdcard` namespace in [main.cpp](../src/main.cpp) initializes Arduino's
built-in `SPI` and `SD` libraries on CS10/MOSI11/SCK12/MISO13 at 1 MHz.
This is project-configured from the vendor wiring and example, **not yet
physically verified**. It does not change display timing or touch configuration.
GPIO11-13 remain shared with P2 and the optional resistive-touch alternative;
do not attach an independently driven bus to those contacts.

Insert a FAT16/FAT32-formatted card before power-on/reset. Boot attempts one
mount at `/sd` with formatting disabled, checks that the root directory opens,
and logs card type/capacity plus up to 16 root entries without recursion.
The validation screen displays the mount result. Missing cards, unsupported
filesystems, and mount/root-open failures release the SD/SPI resources and
leave the display and capacitive touch usable. Boot diagnostics read directory
metadata only; they do not create, overwrite, or delete files.

After successful initialization, application code on the Arduino task can use
the standard `SD.open("/filename", FILE_READ)` or `FILE_WRITE` APIs and must
check open/read/write results and close files. Arduino SD paths are relative
to the card root; `/sd` is the VFS mount point. No new dependency or internal
flash partition is required for this external filesystem. File writes and
LVGL filesystem/image integration are not exercised by this boot demonstration.

There is no wired card-detect input or hot-plug service. The status is a boot
snapshot; power off before swapping cards and reboot to remount. Physical
validation still needs a known card, absent/unsupported-card boots, file
read/write verification, and display/touch stability checks during SD activity.

### I2S audio and speaker

| Signal | GPIO / connection |
| --- | --- |
| ESP32 DOUT -> amplifier DIN | 17 |
| LRCLK | 18 |
| BCLK | V1.0: 19; V1.1: 0 |
| Amplifier label | LCM U4: `max98357`, without an A/B suffix |
| Speaker P6 contact 1 / 2 | OUTP / OUTN through filtering; differential speaker output |
| Amplifier power / SD_MD path | VBUS; VIN-only operation unverified |

Sources: both LCM drawings, lower-center audio block; [revision note][revision];
[7.2 player definitions][audio-pins], lines 10-12. The standalone
[audio test][audio-test], lines 23-27, uses BCLK19, consistent with the older
wiring; the music examples use BCLK0. Select by verified PCB revision.
P6 is not a signal/GND expansion header. The bundled audio PDF's filename starts
`AX98357AETE`, but its contents describe MAX98357A/MAX98357B; it does not identify
the exact installed IC suffix.

### USB, UART, and buttons

| Function | Vendor wiring / current project behavior |
| --- | --- |
| Board USB-C data | Routes to CH340C, which bridges to UART0; not a direct native ESP32 USB connection |
| UART0 TX / RX | ESP32 GPIO43 / GPIO44, module TXD0 / RXD0; shared with CH340C and P1 |
| Automatic programming | CH340C DTR/RTS drive the BOOT/reset transistor circuit |
| RESET button | Pulls ESP32 EN to GND |
| BOOT button | Pulls GPIO0 to GND |
| Native ESP32 USB D- / D+ | GPIO19 / GPIO20; already used for touch/expansion and older audio wiring |
| Project serial | `Serial.begin(115200)`, `ARDUINO_USB_MODE=1`; USB CDC on boot is commented out, so `Serial` uses UART0 in this baseline |

Sources: MCU schematic upper-right USB/CH340C and auto-download sections;
LCM upper-left EN/BOOT section; module datasheet pp. 10-12;
[project configuration](../platformio.ini). The USB mode define does not change
the physical USB-C routing.

### Expansion connector contacts

These are **schematic contact numbers**, not physical left-to-right positions.
Connector orientation, cable color conventions, actual fitted connector types,
and allowable supply current remain unverified. All signals already have other
board connections.

| Port / schematic type | Contact 1 | Contact 2 | Contact 3 | Contact 4 |
| --- | --- | --- | --- | --- |
| P1 / HC1.25 | VIN | U0TXD via R6, 100 ohm | U0RXD via R7, 100 ohm | GND |
| P2 / HC1.25 | GPIO19 | GPIO11 | GPIO12 | GPIO13 |
| P3 / HC1.25 | GPIO17 | GPIO18 | GPIO19 | GPIO20 |
| P4 / HC1.25 | GND | 3.3 V | GPIO17 | GPIO18 |
| P5 / SH1.0 | GND | 3.3 V | GPIO17 | GPIO18 |

Source: [MCU V1.1 drawing][mcu11], P1 upper-left and P2/P3/P4/P5 in the
Extended IO block near the center. P4 and P5 duplicate the same nets. P1's local `TXD2`/`RXD2`
labels conflict with its connected `U0TXD`/`U0RXD` net names; these are UART0 nets,
not evidence of a second independent UART.

### Optional resistive-touch drawing

The LCM lower-right XPT2046 alternative uses GPIO12 CLK, GPIO11 DIN,
GPIO13 DOUT, GPIO38 CS, and GPIO18 IRQ. It shares the SD SPI bus and uses
the same reset/CS and IRQ signals as the capacitive alternative. A schematic
block or commented-out example is not evidence that this option is fitted.

## GPIO ownership and integration constraints

| GPIO(s) | Existing connections / consequence |
| --- | --- |
| 1, 3-9, 14-16, 21, 45-48 | RGB data; includes GPIO16 and GPIO4 used by incompatible LED examples |
| 39-42 | RGB sync, DE, pixel clock; also chip JTAG-capable pins, already occupied here |
| 2 | Backlight enable |
| 10-13 | SD SPI; 11-13 also P2 and optional resistive touch |
| 17 | Audio DIN and P3/P4/P5 |
| 18 | Audio LRCLK, touch IRQ via R17, and P3/P4/P5 |
| 19 | Capacitive SDA, native USB D-, P2/P3, and V1.0 audio BCLK |
| 20 | Capacitive SCL, native USB D+, and P3 |
| 0 | BOOT and V1.1 audio BCLK; current LCD port explicitly leaves it unused |
| 38 | Capacitive reset or resistive CS |
| 43/44 | UART0 bridge and P1 |
| 35-37 | Reserved for octal PSRAM on R8 modules; marked unused / "QSPI do not use" in LCM drawings |

GPIO0, 3, 45, and 46 are strapping pins (module datasheet pp. 11-13). Their
reset-time levels matter even when they serve other functions after boot.
These tables are an ownership map, not a declaration of spare usable pins.

The vendor [LED definitions][led-pins] claim green16, blue17, red4; these
overlap RGB G4, audio DIN, and RGB B4. Generic relay, LED, and DHT examples
also contain application-specific pin choices. They do not establish onboard
LEDs, relays, or sensors and must not be applied as board initialization.

## PlatformIO, memory, and compatibility

Authoritative project settings are in [platformio.ini](../platformio.ini).
The `esp32-s3-devkitc-1` board name is a generic build profile, not identification
of this display PCB. Its defaults are overridden by the project.

| Setting | Current value |
| --- | --- |
| Environment / board | `esp32-s3-devkitc-1` |
| Platform / framework | `espressif32@7.1.3` / `arduino` |
| Flash size / maximum upload size | `16MB` / `16777216` bytes |
| Flash mode / memory type | `qio` / `qio_opi` |
| PSRAM type / flag | `opi` / `BOARD_HAS_PSRAM` |
| Partition file | `full_16mb.csv` |
| CPU / flash clocks | Installed board profile defaults: 240 MHz / 80 MHz; not explicit project overrides |
| Serial monitor | 115200 baud |
| Libraries | `lvgl/lvgl@9.6.0`; configuration in `include/lv_conf.h`, no vendor library imports |

Installed software snapshot on 2026-10-15. Platform and LVGL are pinned; transitive
platform package resolution is not fully locked by the project:

| Component | Observed version / local evidence under `$HOME/.platformio` |
| --- | --- |
| PlatformIO Core | 6.2.0, `penv/bin/pio --version` |
| Espressif32 platform | 7.1.3, `platforms/espressif32/platform.json` |
| Arduino-ESP32 | 2.0.17; package `4.20017.260907+sha.dcc1105b`, framework `package.json` and `cores/esp32/esp_arduino_version.h` |
| ESP-IDF SDK | 4.4.7, framework `tools/sdk/esp32s3/include/esp_common/include/esp_idf_version.h` |
| Xtensa ESP32-S3 GCC | `8.4.0+2021r2-patch5`, toolchain package metadata |

The downloaded libraries are a separate historical stack: Arduino_GFX 1.2.8,
LVGL 8.3.0-dev, TAMC_GT911 1.0.2, and ESP32-audioI2S 2.0.0 according to their
`library.properties` files. The bundled `Touch_GT911` fork has no such metadata.
Arduino_GFX's bundled RGB driver uses private ESP-IDF framebuffer internals;
samples also use older LEDC APIs. Do not assume these libraries are drop-in
dependencies for another core version or replace the working native driver
solely because an example uses them.

### Current flash partition limitations

[full_16mb.csv](../full_16mb.csv) contains only:

| Name | Type / subtype | Offset | Size |
| --- | --- | --- | --- |
| app0 | app / ota_0 | 0x10000 | 0xFF0000 (16,711,680 bytes) |

It leaves the initial 64 KiB outside the application partition and uses the
remaining 16 MiB flash address space for the app. There is no NVS partition,
OTA metadata partition, second application slot, or filesystem partition.
The `ota_0` subtype alone does not provide a working OTA arrangement. Plan an
explicit partition change before features requiring NVS-backed settings or
credentials, OTA, or flash files; the SD socket is separate external storage.

Build/upload/monitor commands and the local CLI fallback are in
[AGENTS.md](../AGENTS.md#build-and-validation). The integrated LVGL firmware builds
successfully with the listed stack. The user authorized hardware iteration;
the current 16 MHz image was uploaded through CH340C and verified by esptool.
Serial was read at 115200, and camera-window recordings were captured without audio.
Boot traces still show the known missing NVS/otadata/core-dump partition warnings
from the existing app-only table. This display fix did not change partitioning
or validate persistence/OTA.

## Future physical verification checklist

Software verification on 2026-10-15:

- Clean PlatformIO build passed for `esp32-s3-devkitc-1`, with no compiler warnings.
  Static RAM: 85,384 / 327,680 bytes (26.1%); flash: 643,309 / 16,711,680 bytes (3.8%) in the final hardware build.
  Runtime buffer allocations are additional to the static RAM figure.
- After consolidating the implementation into `main.cpp`, the PlatformIO build
  and all nine host scenarios passed again. Static RAM remained 85,384 bytes;
  flash was 643,389 bytes. That software check preceded the touch-enabled
  upload; the original ten-minute recordings describe the earlier 16 MHz image.
- A temporary host harness linked the actual application/display sources and
  LVGL 9.6.0 against simulated Arduino/LCD/allocation functions. Nine scenarios
  passed: normal UI rendering/animation and full-screen/final-pixel flushes,
  invalid flush bounds, missing PSRAM, panel creation/reset/init failures,
  draw-buffer allocation failure, bitmap-copy failure, and missing frame interrupts.
  RGB565 swatch values and every pixel of the perimeter were checked; the
  software-rendered screen was also visually inspected. These checks exercise
  software contracts, not ESP32 timing, DMA/cache behavior, or physical image quality.

**The ten-minute 16 MHz stability run passed for the tested workload.**
Serial communication, flashing, animation, sampled camera layout checks, and
memory monitoring were performed. Detailed color/contrast and exact edge
inspection remain limited by camera glare and viewing angle. No tear-free
guarantee is made. Record board revision, firmware identity, toolchain, power
source, and result for further checks.

- [ ] Read the PCB revision, module ordering code, and touch option/markings.
- [ ] Confirm connector contact orientation and supply voltage before attaching peripherals.
- [ ] Display red/green/blue/white/black, gradients, corner markers, and all four edges; verify RGB565 order, final column, and stability at the current timing.
- [x] Run the animated validation screen at 16 MHz for at least ten minutes; serial/camera sampling found no persistent drift, stall, or unexpected reset. Recorded callbacks were close to nominal 28.86 Hz.
- [ ] Inspect every frame for subtle tearing/flicker with a clear, frontal view; current camera glare limits that assessment.
- [x] Record total/free PSRAM and internal memory during the validation UI; memory readings stayed constant throughout the run.
- [x] Detect GT911 at 0x5D and verify real tap/release events and LVGL tap-button clicks.
- [ ] Verify touch cold-start/fallback reset, held/dragged and multi-finger behavior,
  and every corner's axis/orientation; account for the physical GPIO18 IRQ/audio sharing.
- [ ] Mount a known card in SPI mode, test read/write, and check absent-card/mount-failure handling.
- [ ] Confirm audio BCLK against PCB revision and account for GPIO0 BOOT or GPIO19 touch sharing before driving it; test speaker output and USB-powered versus VIN-only behavior.
- [x] Verify serial output at 115200 through CH340C and automatic upload/reset behavior.
- [ ] Verify manual BOOT/RESET recovery if needed; it was not required during these uploads.
- [ ] Validate Wi-Fi/BLE and persistence only after required partition/storage support is in place.
- [ ] Measure mechanical features before enclosure design; do not rely on inconsistent drawing offsets.

## Vendor folder and source index

Bundled vendor source root, relative to this document:
`reference/7.0inch_ESP32-8048S070/`.
The file links below resolve inside the repository. Selected vendor documents,
schematics, examples, companion headers, and library metadata retain their
original names and folder layout. The two cited SDK headers are saved separately
under `reference/arduino-esp32-2.0.17/esp_lcd/include/` from the installed framework
reviewed on 2026-10-15. These are documentation snapshots, not firmware dependencies.

The folder inventory describes the original vendor distribution; only the cited
files and their supporting evidence are bundled. Executables, binary firmware,
and entire vendor libraries are not included. Sources are the supplied snapshot,
not a claim about the latest vendor releases.

| Folder | Contents / use |
| --- | --- |
| `1-Demo/` | Arduino sketches, UI assets, bundled libraries; inspect board-specific initialization and active configuration rather than generic examples. |
| `2-Specification/` | Board overview, product variants, nominal power and size, connector-location photograph. |
| `3-Structure_Diagram/` | Mechanical drawings and photographs; measure disputed dimensions. |
| `4-Driver_IC_Data_Sheet/` | ESP32-S3/module, bare LCD panel, and audio amplifier PDFs. |
| `5-Schematic/` | MCU and LCM drawings for both revisions, module pin image, revision note. |
| `6-User_Manual/` | Arduino IDE setup and vendor demo installation; reference only for this PlatformIO project. |
| `7-Character&Picture_Molding_Tool/` | Archived image/font, serial, network, and driver tools; not build dependencies. |
| `8-Burn operation/` | Prebuilt firmware, Windows flash utility, logs, and flashing screenshots; not executed or flashed during this work. |

| Reference | Exact document / useful location |
| --- | --- |
| [Board specification][spec] | `2-Specification/ESP32-8048S070 Specifications-EN.pdf`: pp. 3-4 capabilities/power, p. 5 connector photograph, p. 6 mechanical labels. |
| [Module datasheet][module] | `4-Driver_IC_Data_Sheet/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf`: pp. 2-3 capabilities/ordering codes, pp. 10-13 pins/PSRAM/strapping. |
| [SoC datasheet][soc] | `4-Driver_IC_Data_Sheet/esp32-s3_datasheet_en.pdf`: ESP32-S3 capabilities and peripheral reference. |
| [Panel datasheet][panel] | `4-Driver_IC_Data_Sheet/JC8048B070N.pdf`: p. 3 bare-panel data, p. 5 contacts, pp. 7-10 timing/backlight. |
| [Audio PDF][audio-pdf] | `4-Driver_IC_Data_Sheet/AX98357AETE T_2021-01-06.PDF`: contents identify MAX98357A/MAX98357B; pp. 15-16 pins/interface. |
| [MCU V1.1][mcu11] / [older MCU][mcu10] | `5-Schematic/ESP32-8048S070-MCU-V1.1.png` / `ESP32-8048S070-MCU-V1.0.jpg`: VIN power upper-left, USB above, SD lower-left, central Extended IO block, backlight lower-right. |
| [LCM V1.1][lcm11] / [LCM V1.0][lcm10] | `5-Schematic/ESP32-8048S070-LCM-V1.1.png` / `ESP32-8048S070-LCM-VV1.0.jpg`: module/buttons upper-left, LCD1 upper-right, capacitive touch middle-right, audio lower-center, resistive alternative lower-right. |
| [Revision note][revision] | `5-Schematic/Version upgrade instructions.txt`: two listed V1.0-to-V1.1 changes. |
| [Mechanical image][dimensions] | `3-Structure_Diagram/Dimensions.png`; companion `1.jpg`. |
| [Getting started][manual] | `6-User_Manual/Getting started 7.0 Inch.pdf`: pp. 8-9 show differing IDE settings; pp. 11-18 demo/library setup. Page 9's 16 MB/OPI settings are consistent with the project's memory choices; its partition screenshot is not the project's partition table. |
| [HelloWorld][hello] | `1-Demo/Demo_Arduino/3_3-1_TFT_HelloWorld/HelloWorld/HelloWorld.ino`: lines 18-35 pins/timing, 47-49 backlight. |
| [Widgets touch][touch] | `1-Demo/Demo_Arduino/3_3-4_TFT-LVGL-Widgets/LvglWidgets/touch.h`: lines 19-29 settings, 52-55 driver, 115-168 startup/polling/mapping. |
| [Music sketch][music] / [music touch wrapper][music-touch] | `1-Demo/Demo_Arduino/7_1_lvgl_music_gt911_7.0/lvgl_music_gt911_7.0/lvgl_music_gt911_7.0.ino`: lines 35-52 display configuration; adjacent `touch.h`: lines 52-55 driver selection, 163-164 coordinate mapping. |
| [SD music sketch][music-sd] | `1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink.ino`: lines 150-154 LEDC API usage and commented SD initialization. |
| [TAMC driver][tamc] / [music driver][touch-driver] | `1-Demo/Demo_Arduino/Libraries/Gt911-arduino-main/TAMC_GT911.cpp` and `Libraries/Touch_GT911/Touch_GT911.cpp`; adjacent headers define addresses and rotations. |
| [SD pins][sd-pins] / [SD init][sd-init] | `1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/HAL.h`: lines 11-14; `HAL.cpp`: lines 9-24. |
| [Music audio pins][audio-pins] / [LED pins][led-pins] | Same 7.2 sketch directory, `player.h`: lines 10-12 and `doLED.h`: lines 1-3. |
| [Standalone audio test][audio-test] | `1-Demo/Demo_Arduino/6_1_Audio_test.ino/Audio_test.ino/Audio_test.ino.ino`: lines 23-27 older audio pin selection. |
| [Bundled RGB driver][gfx-driver] | `1-Demo/Demo_Arduino/Libraries/Arduino_GFX-master/src/databus/Arduino_ESP32RGBPanel.cpp`: lines 259-261 private framebuffer access. Library version evidence is retained in the copied `library.properties` files under the same `Libraries/` tree. |

[spec]: <reference/7.0inch_ESP32-8048S070/2-Specification/ESP32-8048S070 Specifications-EN.pdf>
[module]: <reference/7.0inch_ESP32-8048S070/4-Driver_IC_Data_Sheet/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf>
[soc]: <reference/7.0inch_ESP32-8048S070/4-Driver_IC_Data_Sheet/esp32-s3_datasheet_en.pdf>
[panel]: <reference/7.0inch_ESP32-8048S070/4-Driver_IC_Data_Sheet/JC8048B070N.pdf>
[audio-pdf]: <reference/7.0inch_ESP32-8048S070/4-Driver_IC_Data_Sheet/AX98357AETE T_2021-01-06.PDF>
[mcu11]: <reference/7.0inch_ESP32-8048S070/5-Schematic/ESP32-8048S070-MCU-V1.1.png>
[mcu10]: <reference/7.0inch_ESP32-8048S070/5-Schematic/ESP32-8048S070-MCU-V1.0.jpg>
[lcm11]: <reference/7.0inch_ESP32-8048S070/5-Schematic/ESP32-8048S070-LCM-V1.1.png>
[lcm10]: <reference/7.0inch_ESP32-8048S070/5-Schematic/ESP32-8048S070-LCM-VV1.0.jpg>
[revision]: <reference/7.0inch_ESP32-8048S070/5-Schematic/Version upgrade instructions.txt>
[dimensions]: <reference/7.0inch_ESP32-8048S070/3-Structure_Diagram/Dimensions.png>
[manual]: <reference/7.0inch_ESP32-8048S070/6-User_Manual/Getting started 7.0 Inch.pdf>
[hello]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/3_3-1_TFT_HelloWorld/HelloWorld/HelloWorld.ino>
[touch]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/3_3-4_TFT-LVGL-Widgets/LvglWidgets/touch.h>
[music]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_1_lvgl_music_gt911_7.0/lvgl_music_gt911_7.0/lvgl_music_gt911_7.0.ino>
[music-touch]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_1_lvgl_music_gt911_7.0/lvgl_music_gt911_7.0/touch.h>
[music-sd]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink.ino>
[gfx-driver]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/Libraries/Arduino_GFX-master/src/databus/Arduino_ESP32RGBPanel.cpp>
[tamc]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/Libraries/Gt911-arduino-main/TAMC_GT911.cpp>
[touch-driver]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/Libraries/Touch_GT911/Touch_GT911.cpp>
[sd-pins]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/HAL.h>
[sd-init]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/HAL.cpp>
[audio-pins]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/player.h>
[led-pins]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/7_2_lvgl_music_gt911_7.0__sd_with_blink/lvgl_music_gt911_7.0__sd_with_blink/doLED.h>
[audio-test]: <reference/7.0inch_ESP32-8048S070/1-Demo/Demo_Arduino/6_1_Audio_test.ino/Audio_test.ino/Audio_test.ino.ino>
[rgb-header]: <reference/arduino-esp32-2.0.17/esp_lcd/include/esp_lcd_panel_rgb.h>
[ops-header]: <reference/arduino-esp32-2.0.17/esp_lcd/include/esp_lcd_panel_ops.h>
