//! Latitude 5420 boot checklist, mirroring latitude5420.c.
use crate::{cpu, pci};
pub struct BootCheck {
    pub tiger_lake: bool, pub nvme: bool, pub xhci: bool, pub serial_io: bool,
}
pub fn check(family: u32, model: u32, vendor_intel: bool, devices: &[(u8, u8, u8)]) -> BootCheck {
    BootCheck {
        tiger_lake: cpu::is_tiger_lake(vendor_intel, family, model),
        nvme: devices.iter().any(|&(c, s, p)| pci::is_nvme(c, s, p)),
        xhci: devices.iter().any(|&(c, s, p)| pci::is_xhci(c, s, p)),
        serial_io: devices.iter().any(|&(c, s, _)| pci::is_serial_io_i2c(c, s)),
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn checklist_5420() {
        let c = check(6, 140, true, &[(0x01, 0x08, 0x02), (0x0C, 0x03, 0x30), (0x0C, 0x80, 0x00)]);
        assert!(c.tiger_lake && c.nvme && c.xhci && c.serial_io);
    }
}
