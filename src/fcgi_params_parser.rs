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

// Ported from the C test suite that predates the Rust rewrite
// (tests/test_fcgi_params_parser.c, tests/test_parse_script_filename.c).
#[cfg(test)]
mod tests {
    use super::*;

    // Two pairs with 4-byte length prefixes: NAMEW=12345678, 1111W=76543210
    const INPUT: &[u8] = b"\x80\x00\x00\x05\x80\x00\x00\x08NAMEW12345678\x80\x00\x00\x05\x80\x00\x00\x081111W76543210";

    fn drain(parser: &mut FcgiParamsParser, into: &mut Vec<(String, String)>) {
        while let Some((key, value)) = parser.next_pair() {
            into.push((key.to_owned(), value.to_owned()));
        }
    }

    fn expected() -> Vec<(String, String)> {
        vec![
            ("NAMEW".to_owned(), "12345678".to_owned()),
            ("1111W".to_owned(), "76543210".to_owned()),
        ]
    }

    #[test]
    fn parses_pairs_all_at_once() {
        let mut parser = FcgiParamsParser::new();
        parser.put(INPUT);
        let mut got = Vec::new();
        drain(&mut parser, &mut got);
        assert_eq!(got, expected());
    }

    #[test]
    fn parses_pairs_fed_in_fragments() {
        let fragments: [&[u8]; 5] = [
            b"\x80\x00\x00\x05",
            b"\x80\x00\x00\x08",
            b"NAMEW12345678",
            b"\x80\x00\x00\x05\x80\x00\x00\x08",
            b"1111W76543210",
        ];
        let mut parser = FcgiParamsParser::new();
        let mut got = Vec::new();
        for fragment in fragments {
            parser.put(fragment);
            drain(&mut parser, &mut got);
        }
        assert_eq!(got, expected());
    }

    #[test]
    fn parses_pairs_byte_by_byte() {
        let mut parser = FcgiParamsParser::new();
        let mut got = Vec::new();
        for byte in INPUT {
            parser.put(std::slice::from_ref(byte));
            drain(&mut parser, &mut got);
        }
        assert_eq!(got, expected());
    }

    // FCGI_PARAMS record content captured from nginx 1.18.
    const NGINX_PARAMS: &[u8] = b"\x09\x00PATH_INFO\
        \x0f\x17SCRIPT_FILENAME/var/www/html/test.fcgi\
        \x0c\x00QUERY_STRING\
        \x0e\x03REQUEST_METHODGET\
        \x0c\x00CONTENT_TYPE\
        \x0e\x00CONTENT_LENGTH\
        \x0b\x0aSCRIPT_NAME/test.fcgi\
        \x0b\x0aREQUEST_URI/test.fcgi\
        \x0c\x0aDOCUMENT_URI/test.fcgi\
        \x0d\x0dDOCUMENT_ROOT/var/www/html\
        \x0f\x08SERVER_PROTOCOLHTTP/1.1\
        \x0e\x04REQUEST_SCHEMEhttp\
        \x11\x07GATEWAY_INTERFACECGI/1.1\
        \x0f\x0cSERVER_SOFTWAREnginx/1.18.0\
        \x0b\x09REMOTE_ADDR127.0.0.1\
        \x0b\x05REMOTE_PORT44328\
        \x0b\x00REMOTE_USER\
        \x0b\x09SERVER_ADDR127.0.0.1\
        \x0b\x02SERVER_PORT80\
        \x0b\x01SERVER_NAME_\
        \x0f\x03REDIRECT_STATUS200\
        \x09\x09HTTP_HOSTlocalhost\
        \x0f\x0bHTTP_USER_AGENTcurl/7.74.0\
        \x0b\x03HTTP_ACCEPT*/*";

    #[test]
    fn extracts_script_filename_from_captured_nginx_params() {
        for feed_byte_by_byte in [false, true] {
            let mut parser = FcgiParamsParser::new();
            let mut got = Vec::new();
            if feed_byte_by_byte {
                for byte in NGINX_PARAMS {
                    parser.put(std::slice::from_ref(byte));
                    drain(&mut parser, &mut got);
                }
            } else {
                parser.put(NGINX_PARAMS);
                drain(&mut parser, &mut got);
            }
            assert_eq!(got.len(), 24);
            let find = |key: &str| {
                got.iter().find(|(k, _)| k == key).map(|(_, v)| v.as_str())
            };
            assert_eq!(find("SCRIPT_FILENAME"), Some("/var/www/html/test.fcgi"));
            assert_eq!(find("PATH_INFO"), Some(""));
            assert_eq!(find("REQUEST_METHOD"), Some("GET"));
        }
    }
}
