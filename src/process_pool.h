#pragma once
#include <event2/event.h>
#include "fcgi_process.h"

bool pool_init(const char *path);
fcgi_process_t *pool_borrow_process(struct event_base *base, const char *path);
void pool_release_process(fcgi_process_t *proc);
void pool_start_inactivity_detector(struct event_base *base);
