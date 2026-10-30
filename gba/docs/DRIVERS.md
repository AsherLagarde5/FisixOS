# FisixGBA drivers — PC FisixOS vs GBA

| PC FisixOS driver | GBA replacement | Status in v1 |
|---|---|---|
| EFI loader, GOP | GBA header + entry at 0x08000000, no loader | Boot directly from cart |
| ACPI / PCI scan | None — hardware is fixed, registers are fixed | Not needed |
| NVMe / SATA disk | Cart flash, paged (see CART.md) | Abstraction in cart.c |
| xHCI USB mouse/keyboard | D-pad + A/B/Start/Select, KEYINPUT | Implemented, polled |
| PS/2 keyboard | On-screen keyboard for setup (D-pad + A) | Stub in main.c |
| Iris Xe / GOP graphics | LCD 240x160, Mode 3 bitmap v1, Mode 0 tiles later | Mode 3 in video.c |
| Ethernet / Wi-Fi / LTE | Link cable SIO, FisixLink (see LINK.md) | Protocol in link.c |
| Serial UART | SIO UART mode is v2, Normal mode is v1 | Normal mode only |
| RTC | No GBA RTC on stock hardware; cart RTC is v2 | Ticks only in v1 |

Key registers used (all original code, standard public GBA docs):

- DISPCNT 0x04000000, VRAM 0x06000000, Palette 0x05000000
- KEYINPUT 0x04000130 (bits active-low: 0=A,1=B,2=Select,3=Start,
  4=Right,5=Left,6=Up,7=Down,8=R,9=L)
- TM0CNT 0x04000102 etc., timers at 0x04000100
- SIOCNT 0x04000128, SIODATA8 0x0400012A, SIOMLT_SEND 0x0400012A
  in multiplayer mode (not v1)
- ROM 0x08000000-0x09FFFFFF (32 MB window)
- SRAM 0x0E000000 (stock saves; NOT the 8 GB store)
- EWRAM 0x02000000 (256 KB), IWRAM 0x03000000 (32 KB)

No BIOS calls are used in v1. Everything is direct register I/O,
so there is no Nintendo BIOS dependency to copy or bundle.
