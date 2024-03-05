#pragma once
#include "fcgi_process.h"

fcgi_process_t *pool_borrow_process(const char *path);
void pool_release_process(fcgi_process_t *proc);
