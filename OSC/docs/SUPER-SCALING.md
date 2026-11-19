# Super Scaling — FisixOS resolution scaling (2026-11-19)

An option called **Super Scaling** that automatically detects the
resolution and doesn't need a driver no matter what, so even if
something is designed for 720p it can go up to 32K.

## How it works

- The GUI is designed once, at **1280x720 (720p)**.
- At boot, Super Scaling reads the native resolution the firmware
  already set — the UEFI GOP mode, i.e. the framebuffer that is
  already there. No driver, no GPU, no mode-setting, no probing.
- It scales the 720p picture to fit the native framebuffer with
  nearest-neighbour sampling, in plain CPU code
  (`OSC/kernel/drivers/superscaling.c`), wired into the LVGL
  flush path (`lvgl_port.c`): LVGL renders at 720p logical, the
  flush scales each redrawn area to native.
- The pointer is mapped back the other way: native framebuffer
  coordinates from `gui.c` are converted to logical 720p
  coordinates in `lvgl_port_feed_pointer()`, so clicks land
  where the picture actually is.

## Any resolution

The scale factor is fractional fixed-point (16.16, rounded), never
assumed to be a whole number and never assumed to be a standard
size. It works for:

- Normal: 1080p (1.5x), 4K (3x), 8K (6x)
- Odd: **5K (5120x2880, 4x), 6K (6144x3456, 4.8x), 7K (7168x4032, 5.6x)** — all land exact, no lost pixel
- Huge: up to **32K (32768 x 18432, 25.6x)**
- Weird low: **200p (352x200)** — scales *down* too, picture still
  shows, just coarser (downscaling drops pixels, as it must)
- Ultrawide / odd aspect: scaled to fit, **letterboxed, never
  stretched** — a 16:9 design stays 16:9, black bars fill the rest.
  The bars are cleared black once at init, so they aren't stale
  framebuffer contents.

Above 32K, Super Scaling switches itself off and the GUI runs
native instead of writing past the framebuffer. At exactly
1280x720 it is also off (nothing to scale). With the option off
(`superscaling_set_enabled(0)`), the GUI runs at native
resolution directly. The toggle takes effect on the next
`lvgl_port_init` (next boot).

## Honest limits

- Nearest-neighbour only — no bilinear/filtered scaling yet.
  Upscaling is crisp (source pixels just get bigger); downscaling
  to something like 200p drops pixels.
- Only the LVGL Start screen goes through Super Scaling in v1.
  The old `gui.c` GUI still draws at native resolution.
- The framebuffer still has to exist and fit in RAM: a 32K
  framebuffer at 32bpp is ~2.4 GB, so a real 32K display needs
  far more than the 128 MB minimum RAM. Super Scaling does not
  magic RAM into existence.
- No QEMU boot test (QEMU not installed on this VM) and nothing
  on real hardware yet.

## Verification (2026-11-19)

- Host test (`/tmp/superscale-test`, not in repo): geometry
  exact at 5K, 6K, 7K, 4K, 8K, 32K, 1080p, 200p, ultrawide
  5K letterbox, 720p passthrough, over-32K switch-off, disabled
  passthrough; full-frame 720p -> 1440p pixels pixel-exact;
  200p downscale mapping checked; pointer mapping checked.
  Two real bugs found and fixed by the test: fixed-point
  truncation losing a pixel at odd widths, and the dest rect
  coming up 1 pixel short at 200p. Result: ALL PASS.
- `make kernel` and `make all` exit 0, no new warnings,
  including a `FISIX_USE_LVGL=0` build.
