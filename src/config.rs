use configparser::ini::Ini;
use std::collections::HashSet;
use std::path::Path;
use log2::*;

pub struct Config {
    pub worker_threads: u64,
    pub listen_path: String,
    pub pool_path: String,
    pub log_level: String,
    pub fcgi_extensions: HashSet<String>,
    pub process_idle_timeout: u64,
}

impl Config {
   pub fn load() -> Result<Config, String> {
       let args: Vec<String> = std::env::args().collect();
       let cfg_path = args.get(1).map_or("/etc/std-fpm.conf", String::as_str);

       let mut cfg = Ini::new();
       cfg.load(cfg_path)?;
   
       let worker_threads = cfg.getuint("global", "worker_threads")?.unwrap_or(0);
       let listen_path = cfg.get("global", "listen").ok_or("listen path not specified")?;
       let pool_path = cfg.get("global", "pool").ok_or("pool path not specified")?;
       let fcgi_extensions = cfg.get("global", "fcgi_extensions").ok_or("fcgi_extensions not specified")?.to_lowercase();
       let log_level = cfg.get("global", "log_level").unwrap_or(String::from("info"));
       let process_idle_timeout = cfg.getuint("global", "process_idle_timeout")?.ok_or("process_idle_timeout not specified")?;
       info!("Using config: {}", &cfg_path);

       if let Some(dir) = Path::new(&listen_path).parent() {
           let _ = std::fs::create_dir_all(&dir);
       }

       let _ = std::fs::create_dir_all(&pool_path);

       let cfg = Config {
           worker_threads, listen_path, pool_path, process_idle_timeout, log_level,
           fcgi_extensions: split_extensions(&fcgi_extensions),
       };
       Ok(cfg)
   }
}

fn split_extensions(s : &str) -> HashSet<String> {
    HashSet::from_iter(s.split(';').map(|s| s.trim().trim_start_matches('.').to_owned()))
}


