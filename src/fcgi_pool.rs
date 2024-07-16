use std::collections::{ VecDeque, HashMap };
use std::time::Instant;
use tokio::process::Child;
use log2::*;

pub struct FcgiPool {
    pool: HashMap<String, VecDeque<FcgiProcess>>,
    ctr: u64,
}

pub struct FcgiProcess {
    pub child: Child,
    pub socket_path: String,
    pub ts: Instant,
}

impl Drop for FcgiProcess {
    fn drop(&mut self) {
        if self.socket_path.len() > 0 {
            debug!("Removing socket: {}", self.socket_path);
            let _ = std::fs::remove_file(&self.socket_path).map_err(|err| {
                warn!("Failed while removing socket: {}", err);
            });
        }
    }
}

pub enum Filter {
    OlderThan(u64),
}

impl FcgiPool {
    pub fn new() -> FcgiPool {
        FcgiPool { pool: HashMap::new(), ctr: 0 }
    }
    pub fn take_process(&mut self, script_filename : &str) -> Option<FcgiProcess> {
        self.pool.get_mut(script_filename)?.pop_front()
    }
    pub fn inc_ctr(&mut self) -> u64 {
        self.ctr += 1;
        self.ctr
    }
    pub fn add_process(&mut self, script_filename : &str, proc: FcgiProcess) {
        if !self.pool.contains_key(script_filename) {
            self.pool.insert(script_filename.to_owned(), VecDeque::new());
        }
        self.pool.get_mut(script_filename).unwrap().push_front(proc);
    }
    pub fn remove_processes(&mut self, filter: Filter) -> VecDeque<FcgiProcess> {
        let mut ret = VecDeque::new();
        let now = Instant::now();
        let Filter::OlderThan(secs) = filter;

        for (_script_filename, queue) in self.pool.iter_mut() {
            while let Some(proc) = queue.back() {
                let elapsed = now - proc.ts;
                if elapsed.as_secs() <= secs {
                    break;
                }

                debug!("Terminating process {} ({} seconds passed since last request)",
                    proc.child.id().unwrap_or(0),
                    elapsed.as_secs());

                if let Some(proc) = queue.pop_back() {
                    ret.push_front(proc);
                }
            }
        }

        return ret;
    }
}
