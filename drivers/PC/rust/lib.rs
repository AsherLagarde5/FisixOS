//! Rust drivers for Dell Latitude 5420 / Tiger Lake, mirroring the C drivers
//! in kernel/drivers/. These are host-tested with `cargo test`.
//! They are NOT yet linked into the freestanding C kernel: that step needs
//! a no_std build, a panic handler, and a C/Rust link step, and is not
//! claimed here. The logic (CPUID decoding, PCI config, NVMe/SMBIOS
//! parsing) is the same as the C code, so a future link step reuses it.
pub mod cpu;
pub mod pci;
pub mod nvme;
pub mod smbios;
pub mod latitude5420;
