#pragma once
#include "worker.h"
#include "fd_ctx.h"

void stdfpm_socket_accepted_cb(worker_t *worker, int fd);
