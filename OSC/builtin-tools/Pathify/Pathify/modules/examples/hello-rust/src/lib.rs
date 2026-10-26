use std::ffi::c_char;
use std::slice;

static MODULE_NAME: &[u8] = b"hello-rust\0";
static MODULE_DESCRIPTION: &[u8] = b"Example Rust module that echoes its arguments.\0";

#[unsafe(no_mangle)]
pub extern "system" fn PathifyModuleAbiVersion() -> u32 {
    1
}

#[unsafe(no_mangle)]
pub extern "system" fn PathifyModuleName() -> *const c_char {
    MODULE_NAME.as_ptr().cast()
}

#[unsafe(no_mangle)]
pub extern "system" fn PathifyModuleDescription() -> *const c_char {
    MODULE_DESCRIPTION.as_ptr().cast()
}

#[unsafe(no_mangle)]
pub unsafe extern "system" fn PathifyModuleRun(
    input: *const u8,
    input_length: u32,
    output: *mut u8,
    output_capacity: u32,
) -> i32 {
    if input.is_null() || output.is_null() {
        unsafe {
            write_error(
                output,
                output_capacity,
                "Input and output buffers are required.",
            )
        };
        return -1;
    }

    let input = unsafe { slice::from_raw_parts(input, input_length as usize) };
    let Ok(arguments) = std::str::from_utf8(input) else {
        unsafe { write_error(output, output_capacity, "Arguments must be UTF-8.") };
        return -1;
    };
    let message = format!("hello-rust received JSON arguments: {arguments}\n");
    if message.len() > output_capacity as usize {
        unsafe {
            write_error(
                output,
                output_capacity,
                "Module output buffer is too small.",
            )
        };
        return -1;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(message.as_ptr(), output, message.len());
    }
    message.len() as i32
}

unsafe fn write_error(output: *mut u8, capacity: u32, message: &str) {
    if output.is_null() || capacity == 0 {
        return;
    }
    let count = message.len().min(capacity as usize - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(message.as_ptr(), output, count);
        *output.add(count) = 0;
    }
}
