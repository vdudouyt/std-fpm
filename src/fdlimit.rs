use anyhow::{ Result, anyhow };

pub struct LimitRaised {
    pub from: u64,
    pub to: u64,
}

pub fn raise() -> Result<LimitRaised> {
	unsafe {
		let mut rlim = libc::rlimit { rlim_cur: 0, rlim_max: 0 };
		if libc::getrlimit(libc::RLIMIT_NOFILE, &mut rlim) != 0 {
			Err(anyhow!("getrlimit: {}", std::io::Error::last_os_error()))?;
		}

        let rlim_cur = rlim.rlim_cur;
        rlim.rlim_cur = rlim.rlim_max;

		if libc::setrlimit(libc::RLIMIT_NOFILE, &rlim) != 0 {
			Err(anyhow!("setrlimit: {}", std::io::Error::last_os_error()))?;
        }

        Ok(LimitRaised { from: rlim_cur, to: rlim.rlim_max })
    }
}
