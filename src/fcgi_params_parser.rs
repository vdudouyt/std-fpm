use bytes::{BytesMut, BufMut, Buf};
use std::io::{Cursor};

pub struct FcgiParamsParser {
    buf : BytesMut,
    last_read_len : usize,
}

impl FcgiParamsParser {
    pub fn new() -> FcgiParamsParser {
        FcgiParamsParser { buf: BytesMut::with_capacity(4096), last_read_len: 0 }
    }
    pub fn put(&mut self, src: &[u8]) {
        self.buf.put_slice(src);
    }
    pub fn next_pair(&mut self) -> Option<(&str, &str)> {
        if self.last_read_len > 0 {
            self.buf.advance(self.last_read_len);
            self.last_read_len = 0;
        }
        let mut peeker = Cursor::new(&self.buf[..]);
        let key_len = parse_len(&mut peeker)? as usize;
        let value_len = parse_len(&mut peeker)? as usize;
        if peeker.remaining() < key_len + value_len {
            return None;
        }
        let bytes = &self.buf[peeker.position() as usize..];
        let key = std::str::from_utf8(&bytes[0..key_len]).unwrap();
        let value = std::str::from_utf8(&bytes[key_len..key_len + value_len]).unwrap();
        self.last_read_len = peeker.position() as usize + key_len + value_len;
        return Some((key, value));
    }
}

fn parse_len(peeker: &mut std::io::Cursor<&[u8]>) -> Option<u32> {
    if peeker.remaining() < 1 {
        return None;
    }
    let len1 = peeker.get_u8();
    if len1 & 0x80 == 0 {
        return Some(len1 as u32);
    }
    if peeker.remaining() < 3 {
        return None;
    }
    return Some(((len1 as u32 & 0x7f) << 24) | (peeker.get_uint(3) as u32));
}
