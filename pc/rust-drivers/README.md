# FisixOS Rust drivers - Latitude 5420

Rust mirrors of the C drivers in ../kernel/drivers/, host-tested with
`cargo test` (7 tests pass, rustc 1.75).

Modules: cpu (Tiger Lake detection), pci (config address, NVMe/xHCI/
Serial IO classes, BAR64), nvme (CAP/VS/CSTS), smbios (Type 0 strings),
latitude5420 (boot checklist).

Not yet linked into the freestanding C kernel. Linking needs a no_std
build + panic handler + C link step. Do not claim the kernel runs Rust
code until that step is done and tested in QEMU.
