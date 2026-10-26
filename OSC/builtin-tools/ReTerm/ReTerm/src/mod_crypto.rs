use aes::cipher::{generic_array::GenericArray, BlockEncrypt, KeyInit};
use aes::Aes256;
use hmac::{Hmac, Mac};
use sha2::{Digest, Sha256};
use std::io;
#[cfg(not(windows))]
use std::io::Read;
#[cfg(windows)]
use std::os::raw::{c_int, c_uchar, c_ulong, c_void};

const MASTER_KEY: &[u8] = b"ReTerm module obfuscation key v1 - embedded keys are extractable";
const NONCE_SIZE: usize = 16;
const TAG_SIZE: usize = 32;

type HmacSha256 = Hmac<Sha256>;

pub fn encrypt_mod_payload(plaintext: &[u8]) -> io::Result<Vec<u8>> {
    let mut nonce = [0_u8; NONCE_SIZE];
    fill_random(&mut nonce)?;

    let mut ciphertext = plaintext.to_vec();
    apply_aes256_ctr(&mut ciphertext, &nonce);

    let mut encrypted = Vec::with_capacity(NONCE_SIZE + ciphertext.len() + TAG_SIZE);
    encrypted.extend_from_slice(&nonce);
    encrypted.extend_from_slice(&ciphertext);

    let mut authenticator = <HmacSha256 as Mac>::new_from_slice(&derive_key(b"authentication"))
        .map_err(io::Error::other)?;
    authenticator.update(&encrypted);
    encrypted.extend_from_slice(&authenticator.finalize().into_bytes());
    Ok(encrypted)
}

pub fn decrypt_mod_payload(encrypted: &[u8]) -> io::Result<Vec<u8>> {
    if encrypted.len() < NONCE_SIZE + TAG_SIZE {
        return Err(invalid_data("encrypted module data is truncated"));
    }
    let authenticated_len = encrypted.len() - TAG_SIZE;
    let (authenticated, tag) = encrypted.split_at(authenticated_len);
    let mut authenticator = <HmacSha256 as Mac>::new_from_slice(&derive_key(b"authentication"))
        .map_err(io::Error::other)?;
    authenticator.update(authenticated);
    authenticator
        .verify_slice(tag)
        .map_err(|_| invalid_data("module authentication failed (file is corrupt or modified)"))?;

    let nonce: [u8; NONCE_SIZE] = authenticated[..NONCE_SIZE]
        .try_into()
        .map_err(|_| invalid_data("encrypted module nonce is invalid"))?;
    let mut plaintext = authenticated[NONCE_SIZE..].to_vec();
    apply_aes256_ctr(&mut plaintext, &nonce);
    Ok(plaintext)
}

fn derive_key(purpose: &[u8]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(MASTER_KEY);
    hasher.update(purpose);
    hasher.finalize().into()
}

fn apply_aes256_ctr(data: &mut [u8], nonce: &[u8; NONCE_SIZE]) {
    let cipher =
        Aes256::new_from_slice(&derive_key(b"encryption")).expect("AES-256 accepts a 32-byte key");
    let mut counter = *nonce;
    for chunk in data.chunks_mut(NONCE_SIZE) {
        let mut keystream = GenericArray::clone_from_slice(&counter);
        cipher.encrypt_block(&mut keystream);
        for (byte, mask) in chunk.iter_mut().zip(keystream.iter()) {
            *byte ^= mask;
        }
        increment_counter(&mut counter);
    }
}

fn increment_counter(counter: &mut [u8; NONCE_SIZE]) {
    for byte in counter.iter_mut().rev() {
        let (next, overflow) = byte.overflowing_add(1);
        *byte = next;
        if !overflow {
            break;
        }
    }
}

#[cfg(windows)]
fn fill_random(destination: &mut [u8]) -> io::Result<()> {
    const BCRYPT_USE_SYSTEM_PREFERRED_RNG: c_ulong = 0x00000002;
    #[link(name = "bcrypt")]
    unsafe extern "system" {
        fn BCryptGenRandom(
            algorithm: *mut c_void,
            buffer: *mut c_uchar,
            length: c_ulong,
            flags: c_ulong,
        ) -> c_int;
    }
    let result = unsafe {
        BCryptGenRandom(
            std::ptr::null_mut(),
            destination.as_mut_ptr(),
            destination.len() as c_ulong,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG,
        )
    };
    if result < 0 {
        return Err(io::Error::other(format!(
            "Windows cryptographic random-number generator failed with status {result:#x}"
        )));
    }
    Ok(())
}

#[cfg(not(windows))]
fn fill_random(destination: &mut [u8]) -> io::Result<()> {
    std::fs::File::open("/dev/urandom")?.read_exact(destination)
}

fn invalid_data(message: &'static str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypted_payload_round_trips_and_authenticates() {
        let plaintext = b"private module source";
        let encrypted = encrypt_mod_payload(plaintext).unwrap();
        assert_ne!(encrypted, plaintext);
        assert_eq!(decrypt_mod_payload(&encrypted).unwrap(), plaintext);

        let mut tampered = encrypted;
        tampered[NONCE_SIZE] ^= 1;
        assert!(decrypt_mod_payload(&tampered)
            .unwrap_err()
            .to_string()
            .contains("authentication failed"));
    }
}
