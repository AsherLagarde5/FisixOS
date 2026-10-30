//! PCI config-space address calculation and class parsing.
pub fn config_address(bus: u8, device: u8, function: u8, offset: u8) -> u32 {
    0x8000_0000u32 | ((bus as u32) << 16) | ((device as u32) << 11) | ((function as u32) << 8) | ((offset as u32) & 0xFC)
}
pub fn is_nvme(class_code: u8, subclass: u8, prog_if: u8) -> bool {
    class_code == 0x01 && subclass == 0x08 && prog_if == 0x02
}
pub fn is_xhci(class_code: u8, subclass: u8, prog_if: u8) -> bool {
    class_code == 0x0C && subclass == 0x03 && prog_if == 0x30
}
pub fn is_serial_io_i2c(class_code: u8, subclass: u8) -> bool {
    class_code == 0x0C && subclass == 0x80
}
pub fn bar64(lo: u32, hi: u32) -> u64 {
    if (lo & 0x6) != 0x4 { (lo & 0xFFFF_FFF0) as u64 } else { ((hi as u64) << 32) | ((lo & 0xFFFF_FFF0) as u64) }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn config_addr() {
        assert_eq!(config_address(0, 2, 0, 0x00), 0x8000_1000);
    }
    #[test]
    fn classes() {
        assert!(is_nvme(0x01, 0x08, 0x02));
        assert!(is_xhci(0x0C, 0x03, 0x30));
        assert!(is_serial_io_i2c(0x0C, 0x80));
        assert!(!is_serial_io_i2c(0x0C, 0x03)); // USB is not Serial IO
        assert_eq!(bar64(0x8000_0004, 0x0000_0001), 0x1_8000_0000);
    }
}
