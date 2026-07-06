// FastCGI wire-format helpers shared by tests/functional.rs and the
// stdfpm-test-backend helper binary (each compiles this file as its own
// module, hence the dead_code allowance — they use different subsets).
#![allow(dead_code)]

use std::io::{self, Read};

pub const FCGI_BEGIN_REQUEST: u8 = 1;
pub const FCGI_END_REQUEST: u8 = 3;
pub const FCGI_PARAMS: u8 = 4;
pub const FCGI_STDIN: u8 = 5;
pub const FCGI_STDOUT: u8 = 6;
pub const FCGI_STDERR: u8 = 7;

pub fn record(msg_type: u8, request_id: u16, content: &[u8]) -> Vec<u8> {
    assert!(content.len() <= u16::MAX as usize);
    let mut out = Vec::with_capacity(8 + content.len());
    out.push(1); // version
    out.push(msg_type);
    out.extend_from_slice(&request_id.to_be_bytes());
    out.extend_from_slice(&(content.len() as u16).to_be_bytes());
    out.push(0); // padding length
    out.push(0); // reserved
    out.extend_from_slice(content);
    out
}

pub fn begin_request_body() -> [u8; 8] {
    let mut body = [0u8; 8];
    body[0..2].copy_from_slice(&1u16.to_be_bytes()); // role: FCGI_RESPONDER
    body[2] = 0; // flags: no FCGI_KEEP_CONN
    body
}

pub fn end_request_body(app_status: u32) -> [u8; 8] {
    let mut body = [0u8; 8];
    body[0..4].copy_from_slice(&app_status.to_be_bytes());
    body[4] = 0; // protocol status: FCGI_REQUEST_COMPLETE
    body
}

pub fn encode_params(pairs: &[(&str, &str)]) -> Vec<u8> {
    let mut out = Vec::new();
    for (key, value) in pairs {
        for len in [key.len(), value.len()] {
            if len < 128 {
                out.push(len as u8);
            } else {
                out.extend_from_slice(&((len as u32) | 0x8000_0000).to_be_bytes());
            }
        }
        out.extend_from_slice(key.as_bytes());
        out.extend_from_slice(value.as_bytes());
    }
    out
}

pub struct RecordReader<R: Read> {
    inner: R,
}

impl<R: Read> RecordReader<R> {
    pub fn new(inner: R) -> RecordReader<R> {
        RecordReader { inner }
    }

    /// Reads one record, discarding its padding. Returns Ok(None) on a clean
    /// EOF at a record boundary; EOF mid-record is an UnexpectedEof error.
    pub fn next(&mut self) -> io::Result<Option<(u8, u16, Vec<u8>)>> {
        let mut header = [0u8; 8];
        let n = self.inner.read(&mut header)?;
        if n == 0 {
            return Ok(None);
        }
        self.inner.read_exact(&mut header[n..])?;
        let msg_type = header[1];
        let request_id = u16::from_be_bytes([header[2], header[3]]);
        let content_length = u16::from_be_bytes([header[4], header[5]]) as usize;
        let padding_length = header[6] as usize;
        let mut content = vec![0u8; content_length + padding_length];
        self.inner.read_exact(&mut content)?;
        content.truncate(content_length);
        Ok(Some((msg_type, request_id, content)))
    }
}

/// Deterministic response payload: an HTTP-ish header followed by a byte
/// pattern. Single source of truth for byte-exactness assertions — the
/// backend generates it and the tests compare against it.
pub fn deterministic_payload(total_len: usize) -> Vec<u8> {
    const HEADER: &[u8] = b"Status: 200\r\nContent-Type: text/plain\r\n\r\n";
    let mut out = Vec::with_capacity(total_len);
    out.extend_from_slice(&HEADER[..HEADER.len().min(total_len)]);
    while out.len() < total_len {
        out.push(((out.len() * 7) % 251) as u8);
    }
    out
}

pub fn assert_payload_eq(expected: &[u8], got: &[u8]) {
    if expected == got {
        return;
    }
    let n = expected.len().min(got.len());
    let diverge = (0..n).find(|&i| expected[i] != got[i]).unwrap_or(n);
    panic!(
        "payload mismatch: expected {} bytes, got {} bytes, first divergence at offset {} \
         (expected {:02x?}..., got {:02x?}...)",
        expected.len(),
        got.len(),
        diverge,
        &expected[diverge..expected.len().min(diverge + 8)],
        &got[diverge..got.len().min(diverge + 8)],
    );
}
