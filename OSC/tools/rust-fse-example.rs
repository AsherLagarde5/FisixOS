// Rust .fse example, no_std, for target x86_64-unknown-fisix.
// Build (on a host with Rust installed):
//   rustc --target tools/x86_64-unknown-fisix.json --crate-type lib \
//     -C link-arg=-Ttools/fse.ld -C panic=abort \
//     --emit=obj tools/rust-fse-example.rs -o rust-hello.o
// Then link + make_fse.py as in tools/PORTING-GCC-RUST.md.
// This file is a starter, not a full Cargo project yet: Cargo
// running on FisixOS, and a full Rust std for FisixOS, both need
// the kernel syscall ABI in PORTING-GCC-RUST.md first.
#![no_std]
#![no_main]

#[repr(C)]
pub struct FisixApi {
    pub print: extern "C" fn(*const u8),
    pub print_u64: extern "C" fn(u64),
    pub print_newline: extern "C" fn(),
    pub read_key: extern "C" fn() -> i32,
    pub file_read: extern "C" fn(*const u8, *mut u8, u32) -> i32,
    pub file_write: extern "C" fn(*const u8, *const u8, u32) -> i32,
}

#[no_mangle]
pub extern "C" fn fse_main(api: *const FisixApi) -> i32 {
    unsafe {
        let a = &*api;
        (a.print)(b"Hello from FisixOS, from Rust!\0".as_ptr());
        (a.print_newline)();
    }
    0
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
