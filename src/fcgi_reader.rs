use tokio::net::UnixStream;
use tokio::io::{AsyncReadExt, BufReader, ReadHalf};
use bytes::{BytesMut, Buf};
use std::io::Cursor;

pub struct FcgiReader {
    pub rdstream: BufReader<ReadHalf<UnixStream>>,
    buf: BytesMut,
    pos: usize,
}

#[derive(Debug)]
#[allow(dead_code)]
pub struct FcgiHeader {
    pub version : u8,
    pub msg_type : u8,
    pub request_id : u16,
    pub content_length : u16,
    pub padding_length : u8,
}

pub type Result<T> = std::result::Result<T, tokio::io::Error>;

impl FcgiReader {
    pub fn new(socket_r: ReadHalf<UnixStream>) -> FcgiReader {
        FcgiReader {
            rdstream: BufReader::new(socket_r),
            buf: BytesMut::with_capacity(64 * 1024),
            pos: 0,
        }
    }
    pub async fn read_message(&mut self) -> Result<Option<(FcgiHeader, &[u8])>> {
        loop {
            if let Some((hdr, read_len)) = self.parse_header() {
                let content_start = self.pos + 8;
                self.pos += read_len;
                let content_length = hdr.content_length;
                return Ok(Some((hdr, &self.buf[content_start..content_start + content_length as usize])));
            }
            let n = self.rdstream.read_buf(&mut self.buf).await?;
            if n == 0 {
                return Ok(None);
            }
        }
    }
    fn parse_header(&self) -> Option<(FcgiHeader, usize)> {
        let remainder = self.buf.len() - self.pos;
        if remainder < 8 {
            return None;
        }

        let mut peeker = Cursor::new(&self.buf[self.pos..]);
        let version = peeker.get_u8();
        let msg_type = peeker.get_u8();
        let request_id = peeker.get_u16();
        let content_length = peeker.get_u16();
        let padding_length = peeker.get_u8();
        let _reserved = peeker.get_u8();
        let expected_len = peeker.position() + (content_length as u64) + (padding_length as u64);

        if (remainder as u64) < expected_len {
            return None;
        }

        Some((FcgiHeader { version, msg_type, request_id, content_length, padding_length }, expected_len as usize))
    }
    pub fn trim_buf(&mut self) {
        self.buf.advance(self.pos);
        self.pos = 0;
    }
    pub fn get_buf(&self) -> &[u8] {
        return &self.buf[..];
    }
}
