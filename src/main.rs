use std::fs;
use std::os::unix::fs::PermissionsExt;
use tokio::net::{ UnixListener, UnixStream };
use tokio::process::{ Command, Child };
use std::process::Stdio;

use tokio::io::{ AsyncWriteExt };
use std::path::Path;
use std::sync::{ Arc, Mutex };
use std::io::{Error, ErrorKind};
use std::time::{ Duration, Instant };
use log2::*;

use crate::config::Config;
use crate::fcgi_pool::{ FcgiPool, FcgiProcess };
use crate::fcgi_reader::FcgiReader;
use crate::fcgi_writer::FcgiWriter;
use crate::fcgi_params_parser::FcgiParamsParser;
use crate::fcgi_spawn::fcgi_spawn;

mod fcgi_reader;
mod fcgi_writer;
mod fcgi_params_parser;
mod fcgi_pool;
mod fcgi_spawn;
mod config;

#[tokio::main]
async fn main() -> tokio::io::Result<()> {
    let cfg = Arc::new(Config::load());
    let _ = fs::remove_file(&cfg.listen_path);
    let listener = UnixListener::bind(&cfg.listen_path)?;
    fs::set_permissions(&cfg.listen_path, fs::Permissions::from_mode(0o666))?;
    info!("Listening at: {}", &cfg.listen_path);
    let pool = Arc::new(Mutex::new(FcgiPool::new()));

    {
       let pool = Arc::clone(&pool);
       let cfg = Arc::clone(&cfg);
       start_inactive_process_cleaner(pool, cfg);
    }

    loop {
        let (socket, _) = listener.accept().await?;
        let pool = Arc::clone(&pool);
        let cfg = Arc::clone(&cfg);

        tokio::spawn(async move {
            if let Err(err) = process_conn(socket, cfg, pool).await {
                warn!("{}", err);
            }
        });
    }
}

fn start_inactive_process_cleaner(pool: Arc<Mutex<FcgiPool>>, cfg: Arc<Config>) {
    tokio::spawn(async move {
        loop {
            let sleep_secs = if cfg.process_idle_timeout >= 60 { 60 } else { 10 };
            tokio::time::sleep(Duration::from_secs(sleep_secs)).await;

            debug!("Starting process cleaner");
            let removed_list = pool.lock().unwrap().remove_processes(fcgi_pool::Filter::OlderThan(cfg.process_idle_timeout));
            debug!("{} processes to remove", removed_list.len());
        }
    });
}

async fn process_conn(socket : UnixStream, cfg : Arc<Config>, pool: Arc<Mutex<FcgiPool>>) -> Result<(), String> {
    let (socket_r, socket_w) = tokio::io::split(socket);
    let mut reader = FcgiReader::new(socket_r);
    let mut writer = FcgiWriter::new(socket_w);

    let script_filename = fcgi_read_request_params(&mut reader).await.map_err(|err| {
        format!("Failed while processing request: {}", err)
    })?;
    let connect_result = fcgi_connect_process(&pool, &cfg.pool_path, &script_filename).await;
    if let Err(err) = &connect_result {
        let (status, error_message) = match err.kind() {
            ErrorKind::NotFound => (404, "File not found."),
            ErrorKind::PermissionDenied => (403, "Permission denied."),
            _ => (500, "Error occurred."),
        };
        let _ = writer.send_response(format!("Status: {}\nContent-type: text/html\n\n{}", status, error_message).as_bytes()).await;
    }
    let (mut proc, conn) = connect_result.map_err(|err| {
        format!("{}: {}", &script_filename, err)
    })?;
    
    let (mut conn_r, mut conn_w) = tokio::io::split(conn);
    let mut socket_w = writer.take_socket();
    let write_process = tokio::spawn(async move {
        conn_w.write_all(reader.get_buf()).await?;
        tokio::io::copy(&mut reader.rdstream, &mut conn_w).await?;
        conn_w.shutdown().await?;
        return Ok::<(), tokio::io::Error>(());
    });

    let write_peer = tokio::spawn(async move {
        tokio::io::copy(&mut conn_r, &mut socket_w).await?;
        socket_w.shutdown().await?;
        return Ok::<(), tokio::io::Error>(());
    });

    let _ = write_process.await.map_err(|err| {
        format!("Failed while writing process: {}", err.to_string())
    })?;

    let _ = write_peer.await.map_err(|err| {
        format!("Failed while writing peer: {}", err.to_string())
    })?;
    
    proc.ts = Instant::now();
    pool.lock().unwrap().add_process(&script_filename, proc);
    return Ok(());
}

async fn fcgi_read_request_params(reader : &mut fcgi_reader::FcgiReader) -> tokio::io::Result<String> {
    let mut params_parser = FcgiParamsParser::new();
    while let Some((hdr, data)) = reader.read_message().await? {
        if hdr.msg_type == 4 {
            params_parser.put(data);
        }
        if hdr.msg_type == 4 && hdr.content_length == 0 {
            while let Some((key, value)) = params_parser.next_pair() {
                if key == "SCRIPT_FILENAME" {
                    return Ok(value.to_owned());
                }
            }
            break;
        }
    }
    Err(Error::new(ErrorKind::Other, ""))
}

async fn fcgi_connect_process(pool: &Arc<Mutex<FcgiPool>>, pool_path: &str, script_filename: &str) -> tokio::io::Result<(FcgiProcess, UnixStream)> {
    loop {
        let try_pool = pool.lock().unwrap().take_process(script_filename);
        let (proc, retry) = match try_pool {
            Some(proc) => (proc, true),
            None => {
                let idx = pool.lock().unwrap().inc_ctr();
                let socket_path = format!("{}/stdfpm-{}.sock", pool_path, idx);
                (fcgi_spawn(&script_filename, &socket_path)?, false)
            },
        };
        let try_conn = UnixStream::connect(&proc.socket_path).await;
        if !retry || try_conn.is_ok() {
            return Ok((proc, try_conn?));
        }
    }
}
