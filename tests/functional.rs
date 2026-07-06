// End-to-end tests: each test starts a real std-fpm daemon in its own temp
// directory, talks FastCGI to it over the listen socket the way nginx does,
// and lets it spawn stdfpm-test-backend processes (tests/support/) selected
// via <role>.fcgi symlinks.

#[path = "support/fcgi_proto.rs"]
mod fcgi_proto;

use crate::fcgi_proto::*;
use std::collections::HashSet;
use std::fs;
use std::io::{self, Write};
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::{Duration, Instant};

const SOCKET_TIMEOUT: Duration = Duration::from_secs(30);
const REQUEST_DEADLINE: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// Daemon lifecycle

struct TestEnv {
    dir: PathBuf,
    daemon: Child,
    listen: PathBuf,
    pool_dir: PathBuf,
    log_path: PathBuf,
}

static ENV_COUNTER: AtomicU32 = AtomicU32::new(0);

impl TestEnv {
    fn start(process_idle_timeout: u64) -> TestEnv {
        let name = format!("sfpm-{}-{}", std::process::id(), ENV_COUNTER.fetch_add(1, Ordering::SeqCst));
        let mut base = std::env::temp_dir();
        // sun_path is limited to ~107 bytes; make sure the longest pool socket
        // path fits even under an unusually long TMPDIR.
        if base.join(&name).join("pool/stdfpm-99999.sock").as_os_str().len() > 100 {
            base = PathBuf::from("/tmp");
        }
        let dir = base.join(&name);
        let pool_dir = dir.join("pool");
        fs::create_dir_all(&pool_dir).unwrap();
        let listen = dir.join("std-fpm.sock");
        let conf_path = dir.join("std-fpm.conf");
        fs::write(
            &conf_path,
            format!(
                "[global]\nlisten = {}\npool = {}\nfcgi_extensions = .fcgi\n\
                 process_idle_timeout = {}\nlog_level = debug\n",
                listen.display(),
                pool_dir.display(),
                process_idle_timeout,
            ),
        )
        .unwrap();

        let log_path = dir.join("daemon.log");
        let log = fs::File::create(&log_path).unwrap();
        let log_err = log.try_clone().unwrap();
        use std::os::unix::process::CommandExt;
        let daemon = Command::new(env!("CARGO_BIN_EXE_std-fpm"))
            .arg(&conf_path)
            .stdin(Stdio::null())
            .stdout(log)
            .stderr(log_err)
            .current_dir(&dir)
            .process_group(0)
            .spawn()
            .expect("failed to spawn std-fpm");

        let mut env = TestEnv { dir, daemon, listen, pool_dir, log_path };
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            // This probe connection is accepted and dropped without a request,
            // so every daemon.log starts with one benign
            // "No SCRIPT_FILENAME passed" warning.
            if UnixStream::connect(&env.listen).is_ok() {
                return env;
            }
            if let Some(status) = env.daemon.try_wait().unwrap() {
                panic!("std-fpm exited during startup ({}); log:\n{}", status, env.log_tail());
            }
            assert!(Instant::now() < deadline, "std-fpm not listening after 5s; log:\n{}", env.log_tail());
            std::thread::sleep(Duration::from_millis(50));
        }
    }

    /// Returns <dir>/<role>.fcgi, a symlink to the test backend binary.
    fn script(&self, role: &str) -> PathBuf {
        let link = self.dir.join(format!("{}.fcgi", role));
        if fs::symlink_metadata(&link).is_err() {
            std::os::unix::fs::symlink(env!("CARGO_BIN_EXE_stdfpm-test-backend"), &link).unwrap();
        }
        link
    }

    fn pool_sockets(&self) -> Vec<String> {
        let Ok(entries) = fs::read_dir(&self.pool_dir) else { return Vec::new() };
        let mut names: Vec<String> = entries
            .filter_map(|e| e.ok())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .filter(|n| n.starts_with("stdfpm-") && n.ends_with(".sock"))
            .collect();
        names.sort();
        names
    }

    fn log_tail(&self) -> String {
        let log = fs::read_to_string(&self.log_path).unwrap_or_default();
        log[log.len().saturating_sub(8192)..].to_owned()
    }
}

impl Drop for TestEnv {
    fn drop(&mut self) {
        // std-fpm installs no signal handlers, so killing just the daemon
        // would orphan its backends. They share our process group (the daemon
        // was spawned with process_group(0)), so kill the whole group.
        unsafe {
            libc::kill(-(self.daemon.id() as i32), libc::SIGKILL);
        }
        let _ = self.daemon.wait();
        if std::thread::panicking() {
            eprintln!("--- {} daemon.log tail ---\n{}", self.dir.display(), self.log_tail());
        }
        let _ = fs::remove_dir_all(&self.dir);
    }
}

// ---------------------------------------------------------------------------
// FastCGI client (nginx-like: pipelined request, no half-close, read to EOF)

#[derive(Debug)]
struct FcgiResponse {
    stdout: Vec<u8>,
    stderr: Vec<u8>,
    end_request: bool,
    /// The stream ended with ECONNRESET instead of a clean EOF. Expected when
    /// the daemon closes while our unread request body is still queued; the
    /// kernel hands over all data sent before the reset, so this alone is not
    /// a failure — assertions rest on payload content and end_request.
    reset: bool,
}

fn default_params(script: &Path, body_len: usize) -> Vec<Vec<u8>> {
    let script = script.to_str().unwrap().to_owned();
    let content_length = body_len.to_string();
    let mut pairs = vec![
        ("SCRIPT_FILENAME", script.as_str()),
        ("REQUEST_METHOD", if body_len > 0 { "POST" } else { "GET" }),
        ("SERVER_PROTOCOL", "HTTP/1.1"),
    ];
    if body_len > 0 {
        pairs.push(("CONTENT_LENGTH", content_length.as_str()));
    }
    vec![record(FCGI_PARAMS, 1, &encode_params(&pairs))]
}

fn fcgi_request(listen: &Path, script: &Path, body_len: usize) -> io::Result<FcgiResponse> {
    fcgi_request_with(listen, &default_params(script, body_len), body_len, REQUEST_DEADLINE)
}

fn fcgi_request_with(
    listen: &Path,
    param_records: &[Vec<u8>],
    body_len: usize,
    deadline: Duration,
) -> io::Result<FcgiResponse> {
    let stream = UnixStream::connect(listen)?;
    stream.set_read_timeout(Some(SOCKET_TIMEOUT))?;
    stream.set_write_timeout(Some(SOCKET_TIMEOUT))?;

    let mut head = record(FCGI_BEGIN_REQUEST, 1, &begin_request_body());
    for rec in param_records {
        head.extend_from_slice(rec);
    }
    head.extend_from_slice(&record(FCGI_PARAMS, 1, &[]));

    // Stream the request from a separate thread while this thread reads the
    // response (required for large bodies: a backend may answer before — or
    // without — consuming the body, so writes may legitimately hit EPIPE and
    // are best-effort).
    let mut write_half = stream.try_clone()?;
    let writer = std::thread::spawn(move || {
        let _ = (|| -> io::Result<()> {
            write_half.write_all(&head)?;
            let chunk = [b'B'; 32768];
            let mut remaining = body_len;
            while remaining > 0 {
                let n = remaining.min(chunk.len());
                write_half.write_all(&record(FCGI_STDIN, 1, &chunk[..n]))?;
                remaining -= n;
            }
            write_half.write_all(&record(FCGI_STDIN, 1, &[]))
        })();
    });

    let mut response = FcgiResponse { stdout: Vec::new(), stderr: Vec::new(), end_request: false, reset: false };
    let mut reader = RecordReader::new(&stream);
    let hard_deadline = Instant::now() + deadline;
    let result = loop {
        if Instant::now() > hard_deadline {
            break Err(io::Error::new(io::ErrorKind::TimedOut, "request deadline exceeded"));
        }
        match reader.next() {
            Ok(Some((FCGI_STDOUT, 1, content))) => response.stdout.extend_from_slice(&content),
            Ok(Some((FCGI_STDERR, 1, content))) => response.stderr.extend_from_slice(&content),
            Ok(Some((FCGI_END_REQUEST, 1, _))) => response.end_request = true,
            Ok(Some(_)) => {}
            Ok(None) => break Ok(()),
            Err(e) if e.kind() == io::ErrorKind::ConnectionReset => {
                response.reset = true;
                break Ok(());
            }
            Err(e) => break Err(e),
        }
    };
    let _ = writer.join();
    result.map(|_| response)
}

fn assert_ok_payload(env: &TestEnv, script: &Path, body_len: usize, expected: &[u8]) {
    let resp = fcgi_request(&env.listen, script, body_len).expect("request failed");
    assert!(resp.end_request, "END_REQUEST missing after {} bytes", resp.stdout.len());
    assert_payload_eq(expected, &resp.stdout);
}

// ---------------------------------------------------------------------------
// Tests

#[test]
fn baseline_8mib_byte_exact() {
    let env = TestEnv::start(300);
    let script = env.script("responder");
    let expected = deterministic_payload(8 * 1024 * 1024);
    for _ in 0..10 {
        assert_ok_payload(&env, &script, 0, &expected);
    }
}

#[test]
fn pool_reuse_single_process() {
    let env = TestEnv::start(300);
    let script = env.script("pid_responder");
    let mut pids = HashSet::new();
    for _ in 0..12 {
        let resp = fcgi_request(&env.listen, &script, 0).unwrap();
        assert!(resp.end_request);
        let body = String::from_utf8_lossy(&resp.stdout).into_owned();
        let pid = body.rsplit("pid=").next().unwrap().trim().to_owned();
        assert!(!pid.is_empty(), "no pid in response: {:?}", body);
        pids.insert(pid);
        // The process returns to the pool only after the daemon sees our
        // connection close; don't race it with the next request.
        std::thread::sleep(Duration::from_millis(25));
    }
    assert_eq!(pids.len(), 1, "expected one pooled process to serve everything, got pids {:?}", pids);
    assert_eq!(env.pool_sockets(), vec!["stdfpm-1.sock".to_owned()]);
}

// Regression test, response truncation: a backend that answers without ever
// reading the request body makes af_unix flag the daemon's backend connection
// with ECONNRESET; the relay must still deliver every response byte already
// sent. With the old tokio::io::copy relay this dropped up to 8K of the
// response tail (incl. END_REQUEST) in roughly 10% of these requests.
#[test]
fn truncation_sequential_early_responder() {
    let env = TestEnv::start(300);
    let script = env.script("early_responder");
    let expected = deterministic_payload(8 * 1024 * 1024);
    for i in 0..40 {
        let resp = fcgi_request(&env.listen, &script, 4 * 1024 * 1024)
            .unwrap_or_else(|e| panic!("request {} failed: {}", i, e));
        assert!(resp.end_request, "request {}: END_REQUEST missing after {} bytes", i, resp.stdout.len());
        assert_payload_eq(&expected, &resp.stdout);
    }
}

#[test]
fn truncation_concurrent_early_responder() {
    let env = TestEnv::start(300);
    let script = env.script("early_responder");
    let expected = deterministic_payload(8 * 1024 * 1024);
    std::thread::scope(|scope| {
        for _ in 0..8 {
            scope.spawn(|| {
                for i in 0..10 {
                    let resp = fcgi_request(&env.listen, &script, 4 * 1024 * 1024)
                        .unwrap_or_else(|e| panic!("request {} failed: {}", i, e));
                    assert!(resp.end_request, "request {}: END_REQUEST missing after {} bytes", i, resp.stdout.len());
                    assert_payload_eq(&expected, &resp.stdout);
                }
            });
        }
    });
}

// Regression test, stdout freeze: 6 requests x 24K of stdout logging crosses
// the 64K pipe capacity. When backend stdout was a pipe nobody read, the
// backend froze mid-request around the third request.
#[test]
fn stdout_logger_does_not_freeze() {
    let env = TestEnv::start(300);
    let script = env.script("stdout_logger");
    let expected = deterministic_payload(4096);
    for _ in 0..6 {
        assert_ok_payload(&env, &script, 0, &expected);
    }
}

#[test]
fn oneshot_backend_respawned() {
    let env = TestEnv::start(300);
    let script = env.script("oneshot_responder");
    let expected = deterministic_payload(65536);
    // The backend exits after each request; every following request must be
    // served by a fresh process via the pool's connect-retry path. The pause
    // lets the exit finish first: a request fired in the few microseconds
    // between the backend's response and its exit(0) can still connect into
    // the dying listener's backlog and get an empty response (a known
    // limitation of pooling processes that quit voluntarily).
    for _ in 0..5 {
        assert_ok_payload(&env, &script, 0, &expected);
        std::thread::sleep(Duration::from_millis(50));
    }
}

#[test]
fn many_small_responses() {
    let env = TestEnv::start(300);
    let script = env.script("small_responder");
    let expected = deterministic_payload(4096);
    for _ in 0..200 {
        assert_ok_payload(&env, &script, 0, &expected);
    }
}

#[test]
fn unknown_extension_404() {
    let env = TestEnv::start(300);
    let resp = fcgi_request(&env.listen, &env.dir.join("foo.php"), 0).unwrap();
    assert_eq!(resp.stdout, b"Status: 404\nContent-type: text/html\n\nFile not found.");
    assert!(!resp.end_request); // known quirk: error responses carry no END_REQUEST
}

#[test]
fn nonexistent_script_404() {
    let env = TestEnv::start(300);
    let resp = fcgi_request(&env.listen, &env.dir.join("ghost.fcgi"), 0).unwrap();
    assert_eq!(resp.stdout, b"Status: 404\nContent-type: text/html\n\nFile not found.");
    assert!(!resp.end_request);
    assert!(env.pool_sockets().is_empty(), "failed spawn must remove its pool socket");
}

#[test]
fn missing_script_filename_graceful_close() {
    let env = TestEnv::start(300);
    let params = vec![record(FCGI_PARAMS, 1, &encode_params(&[("REQUEST_METHOD", "GET")]))];
    let resp = fcgi_request_with(&env.listen, &params, 0, Duration::from_secs(20)).unwrap();
    assert!(resp.stdout.is_empty(), "unexpected output: {:?}", String::from_utf8_lossy(&resp.stdout));
    assert!(!resp.end_request);
}

#[test]
fn params_split_across_records() {
    let env = TestEnv::start(300);
    let script = env.script("small_responder");
    let blob = encode_params(&[
        ("AAAA", "BBBBBBBB"),
        ("SCRIPT_FILENAME", script.to_str().unwrap()),
        ("REQUEST_METHOD", "GET"),
    ]);
    // Cut the PARAMS payload at hostile offsets: inside the first pair, right
    // after SCRIPT_FILENAME's length prefix, mid-key, and mid-value.
    let mut params = Vec::new();
    let mut prev = 0;
    for cut in [3usize, 16, 24, 45] {
        let cut = cut.min(blob.len());
        if cut > prev {
            params.push(record(FCGI_PARAMS, 1, &blob[prev..cut]));
            prev = cut;
        }
    }
    params.push(record(FCGI_PARAMS, 1, &blob[prev..]));

    let resp = fcgi_request_with(&env.listen, &params, 0, REQUEST_DEADLINE).unwrap();
    assert!(resp.end_request);
    assert_payload_eq(&deterministic_payload(4096), &resp.stdout);
}

#[test]
fn idle_cleaner_reaps_backend() {
    let env = TestEnv::start(1); // seconds; the cleaner ticks every 10s
    let script = env.script("pid_responder");
    let resp = fcgi_request(&env.listen, &script, 0).unwrap();
    assert!(resp.end_request);
    let body = String::from_utf8_lossy(&resp.stdout).into_owned();
    let pid: i32 = body.rsplit("pid=").next().unwrap().trim().parse().unwrap();

    // Worst case: the 1s idle threshold is crossed right after a cleaner tick,
    // so removal lands on the next one, ~12s out. The process may linger as a
    // zombie briefly (tokio reaps kill_on_drop children asynchronously).
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        let sockets_gone = env.pool_sockets().is_empty();
        let proc_gone = match fs::read_to_string(format!("/proc/{}/stat", pid)) {
            Err(_) => true,
            Ok(stat) => stat.rsplit(") ").next().map_or(false, |rest| rest.starts_with('Z')),
        };
        if sockets_gone && proc_gone {
            return;
        }
        assert!(
            Instant::now() < deadline,
            "backend pid {} not reaped within 30s (pool sockets gone: {}, process gone: {})",
            pid, sockets_gone, proc_gone,
        );
        std::thread::sleep(Duration::from_millis(250));
    }
}
