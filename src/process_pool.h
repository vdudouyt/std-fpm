#pragma once
#include <event2/event.h>
#include "fcgi_process.h"

bool pool_init();
fcgi_process_t *pool_borrow_process(const char *path);
void pool_return_process(fcgi_process_t *proc);
void pool_rip_idling();
