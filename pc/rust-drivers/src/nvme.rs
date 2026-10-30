//! NVMe controller register parsing (CAP, VS, CSTS).
pub struct NvmeRegs { pub cap: u64, pub vs: u32, pub csts: u32 }
pub fn ready(regs: &NvmeRegs) -> bool { regs.csts & 0x01 != 0 }
pub fn major_version(vs: u32) -> u32 { vs >> 16 }
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn nvme_ready() {
        let r = NvmeRegs { cap: 0, vs: 0x0001_0400, csts: 1 };
        assert!(ready(&r));
        assert_eq!(major_version(r.vs), 1);
        let r2 = NvmeRegs { cap: 0, vs: 0x0001_0400, csts: 0 };
        assert!(!ready(&r2));
    }
}
