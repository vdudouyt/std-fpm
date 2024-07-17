use configparser::ini::Ini;
use std::collections::HashSet;
use std::path::Path;
use log2::*;

pub struct Config {
    pub worker_threads: u64,
    pub listen_path: String,
    pub pool_path: String,
    pub error_log: String,
    pub fcgi_extensions: HashSet<String>,
    pub process_idle_timeout: u64,
    _log: log2::Handle,
}

impl Config {
   pub fn load() -> Config {
       let args: Vec<String> = std::env::args().collect();
       let cfg_path = args.get(1).map_or("/etc/std-fpm.conf", String::as_str);

       let mut cfg = Ini::new();
       cfg.load(cfg_path).unwrap();
   
       let worker_threads = cfg.getuint("global", "worker_threads").expect("failed to parse").unwrap_or(0);
       let listen_path = cfg.get("global", "listen").expect("listen path not specified");
       let pool_path = cfg.get("global", "pool").expect("pool path not specified");
       let error_log = cfg.get("global", "error_log").expect("error_log not specified");
       let fcgi_extensions = cfg.get("global", "fcgi_extensions").expect("fcgi_extensions not specified").to_lowercase();
       let log_level = cfg.get("global", "log_level").unwrap_or(String::from("info"));
       let process_idle_timeout = cfg.getuint("global", "process_idle_timeout")
        .expect("failed to parse process_idle_timeout")
        .expect("process_idle_timeout not specified");

       let log = log2::open(&error_log).module(false).tee(true).level(log_level).start();
       info!("Using config: {}", &cfg_path);

       if let Some(dir) = Path::new(&listen_path).parent() {
           let _ = std::fs::create_dir_all(&dir);
       }

       let _ = std::fs::create_dir_all(&pool_path);

       return Config {
           worker_threads, listen_path, pool_path, error_log, process_idle_timeout,
           fcgi_extensions: split_extensions(&fcgi_extensions),
           _log: log
       };
   }
}

fn split_extensions(s : &str) -> HashSet<String> {
    HashSet::from_iter(s.split(';').map(|s| s.trim().trim_start_matches('.').to_owned()))
}


