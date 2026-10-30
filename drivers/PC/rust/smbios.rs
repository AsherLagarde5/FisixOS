//! SMBIOS Type 0 string parsing, mirroring smbios.c.
pub fn struct_string<'a>(s: &'a [u8], index: u8) -> Option<&'a str> {
    if s.len() < 4 || index == 0 { return None; }
    let header_len = s[1] as usize;
    let mut p = header_len;
    let mut cur = 1u8;
    while p < s.len() {
        if s[p] == 0 {
            if p + 1 < s.len() && s[p + 1] == 0 { break; }
            cur += 1; p += 1; continue;
        }
        if cur == index {
            let start = p;
            while p < s.len() && s[p] != 0 { p += 1; }
            return std::str::from_utf8(&s[start..p]).ok();
        }
        while p < s.len() && s[p] != 0 { p += 1; }
    }
    None
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn type0_strings() {
        // Type 0, len 20, vendor idx 1, version idx 2, date idx 3
        let mut s = vec![0u8, 20, 0, 0, 1, 2, 0, 0, 3];
        s.extend_from_slice(b"Dell Inc.\0");
        s.extend_from_slice(b"1.56.0\0");
        s.extend_from_slice(b"01/01/2024\0\0");
        // pad header to 20 bytes: we already have 9 + strings, so construct properly:
        // Instead test the string walker on a header_len=9 structure:
        let mut t = vec![0u8, 9, 0, 0, 1, 2, 0, 0, 3];
        t.extend_from_slice(b"Dell Inc.\0");
        t.extend_from_slice(b"1.56.0\0");
        t.extend_from_slice(b"01/01/2024\0\0");
        assert_eq!(struct_string(&t, 1), Some("Dell Inc."));
        assert_eq!(struct_string(&t, 2), Some("1.56.0"));
        assert_eq!(struct_string(&t, 3), Some("01/01/2024"));
        assert_eq!(struct_string(&t, 4), None);
        let _ = s;
    }
}
