# FisixOS PC — Real-hardware optimization, 2026-10-30

M1 port: CANCELLED by Derek 2026-10-30. Do not do M1. Kept ports:
PC, GBA, Dreamcast, plus Apple IIe kept locally (not in fisix-unified).

Target real hardware (not VM-only): Dell Latitude 5420, i5-1145G7
Tiger Lake, Iris Xe, 1 TB Crucial P310 NVMe, I219-LM Ethernet,
AX201 Wi-Fi (CNVi). The ISO build is UEFI x86-64, GOP framebuffer.

## What was already real-hardware, not VM-only

- PCI enumeration via CF8/CFC config space (pci.c) — real, not virtio.
- NVMe driver (nvme.c) — generic NVMe registers, 4K-aligned queues
  in low memory for DMA without an IOMMU on UEFI identity-mapped boots.
- xHCI driver (xhci.c) — generic xHCI, class 0x0C/0x03/0x30.
- UEFI GOP framebuffer (boot/efi_main.c) — firmware framebuffer,
  width/height/pitch passed to the kernel. This is the display path
  on Intel Iris Xe AND on AMD GPUs (see amd_gpu.c).
- ACPI RSDP (acpi.c), APIC base/version (apic.c), PS/2 keyboard,
  SMBIOS, battery — all real-firmware inputs.
- -O2 was already in CFLAGS. Kept, not changed.

## VM-only assumptions found, and what was done

1. Makefile `run` / `run-iso` (QEMU) and the old "VirtualBox Fixed"
   ISO comment — MARKED: those targets are now labelled
   "VM TEST-ONLY — NOT the real-hardware boot path". The real boot
   path is the UEFI ISO (`make iso`), which works on real UEFI
   firmware, VirtualBox, and QEMU.
2. e1000 driver header said "QEMU -nic user,model=e1000" — FIXED
   comment: it is a generic Intel e1000 PCI driver, also usable in
   QEMU. It does NOT cover the Latitude 5420 I219-LM (different
   device) or AX201 Wi-Fi (CNVi). Real 5420 wired net via e1000 =
   NOT claimed; I219 driver and AX201/CNVi driver are TODO.
3. net.c static IP 10.0.2.15 as primary — FIXED: DHCP first.
   `net_init()` now sets g_ip = 0, sends DHCP Discover, and g_ip
   stays 0 until a DHCP ACK. `net_use_static_fallback()` sets
   10.0.2.15/10.0.2.2 only as a QEMU user-net fallback, never as
   primary. Call it only after DHCP has failed/timed out.
4. GUI SHUTDOWN text "HALTING - USE QEMU / MONITOR QUIT OR" — FIXED
   text to "HALTING - REAL HW: / ACPI S5 POWER OFF, / VM FALLBACK: HALT".
   ACPI S5 itself is NOT yet wired: `acpi_poweroff()` in acpi.c
   returns 0, because FADT/S5 parsing from the RSDP is TODO. A guessed
   SLP_EN write was deliberately NOT added — it can hang/reboot real
   firmware. Until S5 is parsed and tested, shutdown falls back to
   the kernel halt loop (`hlt`), and in a VM the operator can use
   the monitor quit. Do not claim ACPI poweroff works yet.
5. xhci.c "64 on QEMU" and nvme.c "in QEMU" comments — FIXED wording
   to "real hardware / QEMU test" / "real UEFI boots and QEMU test VMs".
   No code change: the drivers themselves are generic.
6. COM1 serial debug in efi_main.c, QEMU sendkey keyboard note,
   VirtualBox tablet hover note — MARKED as test notes, not boot
   paths. Serial debug is harmless on real hardware (COM1 may not
   exist, writes go nowhere useful, they do not break boot).

## Real-hardware optimizations added (safe, checked not assumed)

- CPU feature checks: `cpu_has_sse()`, `cpu_has_sse2()`,
  `cpu_has_avx()` in cpu.c, from the already-stored CPUID
  features_edx/ecx. `cpu_has_avx2()` returns 0 for now because
  cpu_info_t does not store leaf 7; amd_cpu.c checks leaf 7 directly.
  Nothing in the kernel assumes SSE/AVX — use these checks before
  any vector path, do not add an AVX path without a check.
- APIC timer, polling only: `apic_timer_init()` in apic.c programs
  divide-by-16 and a large initial count with the LVT timer masked,
  `apic_timer_current()` reads the current count. This counts down
  on real LAPIC hardware without delivering an interrupt.
  IOAPIC redirection + LVT unmasking + timer interrupts = TODO,
  not claimed.
- AMD drivers: see amd_cpu.c/.h, amd_gpu.c/.h, amd_chipset.c/.h,
  amd.c/.h. Detection only, untested on real AMD hardware.

## AMD drivers — status, honest

- amd_cpu: AuthenticAMD CPUID, family/model (Zen / Zen+ / Zen 2 /
  Zen 3 / Zen 4 / Zen 5, model mapping is a heuristic, see file),
  TSC/SSE/SSE2/AVX/AVX2, SVM *present* (CPUID 0x80000001 ECX bit 2).
  SVM is NEVER enabled here.
- amd_gpu: PCI vendor 0x1002 display detection, BAR0 readout.
  Display = UEFI GOP framebuffer fallback. Accelerated = 0 always.
  Full amdgpu-style driver = TODO.
- amd_chipset: PCI vendor 0x1022 detection, SMBus/SATA/xHCI/NVMe
  presence by PCI class. Storage still uses generic nvme.c, USB
  still uses generic xhci.c. AMD-Vi IOMMU: NOT detected, NOT
  enabled — IVRS parsing is TODO, iommu_detected stays 0.
- amd (integration): `amd_init()` fills all three structs.
  amd_present = CPU or GPU or chipset found. If 0, fall back to
  Intel paths (Latitude 5420 / Tiger Lake / Iris Xe / e1000).
  No hardware state is changed by amd_init().

## Still needs real-hardware testing

- AMD CPU/GPU/chipset: any real AMD Zen system, any real Radeon GPU.
  Until then, detection only — do not claim AMD works.
- ACPI S5 poweroff: needs FADT parsing + a real firmware test.
- I219-LM Ethernet driver, AX201/CNVi Wi-Fi driver.
- APIC timer interrupts, IOAPIC redirection.
- Full ISO boot on a real Latitude 5420 (not just QEMU/VirtualBox).
  The 2026-10-30 ISO rebuild below is a build check, not a boot test.
