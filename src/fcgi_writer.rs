use tokio::net::UnixStream;
use tokio::io::{AsyncWriteExt, BufWriter, WriteHalf};


pub struct FcgiWriter {
    wrstream: BufWriter<WriteHalf<UnixStream>>,
}

impl FcgiWriter {
    pub fn new(socket_w: WriteHalf<UnixStream>) -> FcgiWriter {
        FcgiWriter {
            wrstream: BufWriter::new(socket_w),
        }
    }
    pub fn take_socket(self) -> WriteHalf<UnixStream> {
        self.wrstream.into_inner()
    }
    pub async fn send_message(&mut self, msg_type: u8, request_id: u16, content: &[u8]) -> tokio::io::Result<()> {
        let content_length = content.len() as u16;
        let padding_length = ((4 - (content_length % 4)) % 4) as u8;
        let padding = vec![0; 4];
        self.wrstream.write_u8(1).await?; // version
        self.wrstream.write_u8(msg_type).await?;
        self.wrstream.write_u16(request_id).await?;
        self.wrstream.write_u16(content_length).await?;
        self.wrstream.write_u8(padding_length as u8).await?;
        self.wrstream.write_u8(0).await?; // reserved
        self.wrstream.write_all(content).await?;
        self.wrstream.write_all(&padding[0..padding_length as usize]).await?;
        self.wrstream.flush().await?;
        Ok(())
    }
    pub async fn send_response(&mut self, response: &[u8]) -> tokio::io::Result<()> {
        self.send_message(6, 1, response).await?;
        self.send_message(6, 1, &[]).await?;
        Ok(())
    }
}
