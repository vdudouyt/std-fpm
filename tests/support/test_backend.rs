// FastCGI backend used by the functional tests. std-fpm spawns it the same
// way it spawns any application: the listening socket arrives as fd 0 (see
// src/fcgi_spawn.rs) and argv[0] is SCRIPT_FILENAME. The tests create
// symlinks named <role>.fcgi pointing at this binary, so the behavior for a
// request is selected by our own file stem.

mod fcgi_proto;

use crate::fcgi_proto::*;
use std::io::Write;
use std::os::fd::FromRawFd;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::Path;
use std::time::Duration;

const ROLES: &[&str] = &[
    "responder",
    "small_responder",
    "early_responder",
    "oneshot_responder",
    "stdout_logger",
    "pid_responder",
];

fn main() {
    let argv0 = std::env::args().next().unwrap_or_default();
    let role = Path::new(&argv0)
        .file_stem()
        .and_then(|s| s.to_str())
        .unwrap_or_default()
        .to_owned();
    if !ROLES.contains(&role.as_str()) {
        eprintln!("stdfpm-test-backend: unknown role {:?} (from argv[0] {:?})", role, argv0);
        std::process::exit(2);
    }

    let listener = unsafe { UnixListener::from_raw_fd(0) };
    for conn in listener.incoming() {
        let Ok(conn) = conn else { continue };
        let _ = handle(&role, conn);
        if role == "oneshot_responder" {
            std::process::exit(0);
        }
    }
}

fn handle(role: &str, conn: UnixStream) -> std::io::Result<()> {
    let _ = conn.set_read_timeout(Some(Duration::from_secs(30)));
    let _ = conn.set_write_timeout(Some(Duration::from_secs(30)));

    // early_responder answers as soon as the params are in and never touches
    // the request body — the shape that used to trigger response truncation.
    // Everyone else drains the request through the closing empty STDIN record.
    let stop_at = if role == "early_responder" { FCGI_PARAMS } else { FCGI_STDIN };
    let mut reader = RecordReader::new(conn.try_clone()?);
    while let Some((msg_type, _, content)) = reader.next()? {
        if msg_type == stop_at && content.is_empty() {
            break;
        }
    }

    if role == "stdout_logger" {
        // Models a backend that prints diagnostics to stdout. If std-fpm gave
        // us a pipe nobody reads, this blocks forever once 64K accumulates.
        let noise = [b'L'; 24 * 1024];
        let mut stdout = std::io::stdout();
        stdout.write_all(&noise)?;
        stdout.flush()?;
    }

    let payload = match role {
        "responder" | "early_responder" => deterministic_payload(8 * 1024 * 1024),
        "small_responder" | "stdout_logger" => deterministic_payload(4096),
        "oneshot_responder" => deterministic_payload(65536),
        "pid_responder" => format!("Status: 200\r\n\r\npid={}", std::process::id()).into_bytes(),
        _ => unreachable!(),
    };
    send_response(conn, &payload)
}

fn send_response(mut conn: UnixStream, payload: &[u8]) -> std::io::Result<()> {
    for chunk in payload.chunks(32768) {
        conn.write_all(&record(FCGI_STDOUT, 1, chunk))?;
    }
    conn.write_all(&record(FCGI_STDOUT, 1, &[]))?;
    conn.write_all(&record(FCGI_END_REQUEST, 1, &end_request_body(0)))?;
    Ok(())
}
