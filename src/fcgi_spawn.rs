use std::path::Path;
use std::process::Stdio;
use std::os::fd::OwnedFd;
use std::os::unix::net::UnixListener as StdUnixListener;
use std::time::Instant;
use tokio::process::{ Command };
use crate::fcgi_pool::FcgiProcess;

pub fn fcgi_spawn(cmd: &str, socket_path: &str) -> std::io::Result<FcgiProcess> {
    let _ = std::fs::remove_file(socket_path);

    let listener = StdUnixListener::bind(socket_path)?;
    let mut proc = Command::new(cmd);
    proc.kill_on_drop(true);
    proc.stdin(OwnedFd::from(listener));
    proc.stdout(Stdio::piped());
    proc.stderr(Stdio::null());
    let dirname = Path::new(cmd).parent().unwrap_or(Path::new("/")).to_str().unwrap_or("/");
    proc.current_dir(dirname);
    let child = proc.spawn()?;
    return Ok(FcgiProcess { child, socket_path: socket_path.to_owned(), ts: Instant::now() });
}
