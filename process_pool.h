#pragma once
#include "fcgi_process.h"

void pool_init();
fcgi_process_t *pool_borrow_process(const char *path);
void pool_release_process(fcgi_process_t *proc);
void pool_close_inactive_processes(unsigned int max_idling_time);
