#pragma once
#include "conn.h"
#include "config.h"

struct evconnlistener *stdfpm_create_listener(struct event_base *base, const char *sock_path, stdfpm_config_t *config);
