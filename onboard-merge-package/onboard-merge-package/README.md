# esp32-nut single-board merge: Elecrow CrowPanel Advance 2.4"

Adds a new `elecrow-2.4-onboard` PlatformIO environment to esp32-nut that
runs the **full existing firmware** (USB host, NUT server, web server --
all untouched) *plus* an on-screen dashboard, on the Elecrow board itself,
using its second USB-C port's native USB-OTG host capability (GPIO19/20)
instead of the generic devkit's solder-bridge USB-OTG mod.

The original `esp32-s3-standard` environment (generic devkit) is
**unchanged in behavior** -- it's explicitly excluded from ever pulling in
the new display code (see the diff).

## How this was verified

- Pin mapping for the display/touch: taken from Elecrow's own factory
  source (`LovyanGFX_Driver.h`) for this exact board, same as the earlier
  standalone display project.
- The second USB-C port really is wired to the ESP32-S3's native USB
  peripheral: confirmed directly in Elecrow's Eagle schematic
  (`Eagle_SCH&PCB/1.2/ESP32 Display 2.4 inch V1.2.sch`), which shows nets
  `USB_D+`/`USB_D-` tied to `IO19`/`IO20` -- the same fixed pins
  esp32-nut's `USBHostUPS` library already expects.
- GPIO usage: esp32-nut's own application code only ever touches one pin
  directly outside of libraries (`NEOPIXEL_POWER_PIN`, GPIO38, for the
  diagnostic LED on the generic devkit). That's the *same* pin this board
  uses for its LCD backlight, so the merge disables the NeoPixel
  diagnostic-LED subsystem for this board (it doesn't physically exist
  here anyway) and uses on-screen status dots instead. No other GPIO
  conflicts exist between esp32-nut's code and the display's pins.

## What's still unverified -- please check before relying on this

**VBUS / host-mode power.** I could not confirm from the schematic search
whether the OTG port actively drives 5V to power the UPS's USB interface
in host mode, or whether it needs the same kind of manual power
bridging/mod the generic devkit's USB-OTG pads need. If the UPS doesn't
enumerate, this is the first thing to check with a multimeter on that
port's VBUS pin while the board is running in host mode.

## Applying this to your checkout

1. Copy the contents of `new-files/` into your esp32-nut working copy,
   preserving paths (`include/lv_conf.h`, `lib/LcdDisplay/src/*`).
2. Apply the diff from the root of your esp32-nut checkout:
   ```
   git apply onboard-merge.diff
   ```
   (If it doesn't apply cleanly because your checkout has moved on, the
   diff is small -- three files, see below -- and easy to re-apply by
   hand.)
3. Build and flash the new environment:
   ```
   pio run -e elecrow-2.4-onboard -t upload
   ```

## What changed, file by file

- **`platformio.ini`**: added the `elecrow-2.4-onboard` environment
  (16MB flash / octal PSRAM / same `partitions.csv` as the standard
  build) and added `lib_ignore = LcdDisplay` to the existing
  `esp32-s3-standard` and `native` environments so they're never affected.
- **`include/core/main.h`**: one guarded `#include "LcdDisplay.h"` under
  `#ifdef BOARD_HAS_LCD`.
- **`src/core/main.cpp`**: guarded calls to initialize the display, show
  the existing AP/setup-mode screen on it, and refresh the dashboard once
  a second from `usb_ups` directly (no network hop -- this is simpler
  and more reliable than the earlier Wi-Fi-polling design, since the
  display and the UPS data now live in the same process). The NeoPixel
  diagnostic LED is skipped on this board for the pin reason above.
- **New**: `lib/LcdDisplay/` -- the display driver and dashboard UI,
  same visual design (colors/layout) as the standalone display project,
  adapted to read `IUSBHostUPS` directly instead of polling JSON.

## Known limitation carried over

Same font caveat as before: LVGL's built-in Montserrat fonts are used
instead of the web UI's Orbitron/Share Tech Mono, so this compiles
without any font-conversion step. Swap in a converted font later if you
want an exact visual match.

I reviewed this carefully but don't have an ESP32 toolchain available to
hardware-compile it myself -- if PlatformIO or the hardware itself turns
up problems (especially around VBUS above), send them back and I'll fix
them.
