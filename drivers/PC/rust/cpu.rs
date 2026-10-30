//! CPUID decoding, Tiger Lake detection.
pub fn is_intel(vendor_b: u32, vendor_d: u32, vendor_c: u32) -> bool {
    vendor_b == 0x756e6547 && vendor_d == 0x49656e69 && vendor_c == 0x6c65746e
}
/// Decode family/model from CPUID leaf 1 EAX, per Intel spec.
pub fn decode_family_model(eax: u32) -> (u32, u32) {
    let stepping = eax & 0xF;
    let _ = stepping;
    let mut model = (eax >> 4) & 0xF;
    let mut family = (eax >> 8) & 0xF;
    if family == 6 || family == 15 {
        model += ((eax >> 16) & 0xF) << 4;
    }
    if family == 15 {
        family += (eax >> 20) & 0xFF;
    }
    (family, model)
}
pub fn is_tiger_lake(vendor_intel: bool, family: u32, model: u32) -> bool {
    vendor_intel && family == 6 && model == 140
}
/// i5-1145G7 is Tiger Lake-UP3, family 6 model 140, stepping is not the model.
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn tiger_lake_1145g7() {
        // CPUID leaf 1 EAX for family 6, model 140: ext model 8 << 16, model 12, family 6
        let eax = (8u32 << 16) | (6u32 << 8) | (12u32 << 4) | 1u32;
        let (f, m) = decode_family_model(eax);
        assert_eq!((f, m), (6, 140));
        assert!(is_tiger_lake(true, f, m));
        assert!(!is_tiger_lake(false, f, m));
    }
    #[test]
    fn not_tiger_lake() {
        assert!(!is_tiger_lake(true, 6, 106)); // Ice Lake
        assert!(!is_tiger_lake(true, 15, 107)); // QEMU AMD-ish
    }
}
