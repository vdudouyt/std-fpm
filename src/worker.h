#pragma once
#include <event.h>
#include <gmodule.h>
#include "config.h"

struct worker_s;
typedef void (*socket_acceptor_t)(struct worker_s *worker, int fd);

typedef struct worker_s {
   pthread_t thread;
   unsigned int tid;
   struct event_base *base;
   struct event notify_event;
   int notify_receive_fd;
   int notify_send_fd;
   pthread_mutex_t conn_queue_mutex;
   GQueue *conn_queue;
   socket_acceptor_t acceptor_cb;

   stdfpm_config_t *config;
} worker_t;

worker_t *start_worker(socket_acceptor_t acceptor_cb);
void worker_free(worker_t *self);
