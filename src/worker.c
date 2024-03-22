#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "fdutils.h"
#include "worker.h"

static void *worker(void *ptr);
static void worker_socket_accepted_cb(int pipefd, short which, void *arg);
static int tid;

worker_t *start_worker(socket_acceptor_t acceptor_cb) {
   int fds[2];
   if(pipe(fds) == -1) { perror("pipe"); return NULL; };
   fd_setcloseonexec(fds[0]);
   fd_setcloseonexec(fds[1]);

   worker_t *self = malloc(sizeof(worker_t));
   if(!self) return NULL;
   memset(self, 0, sizeof(worker_t));

   self->conn_queue = g_queue_new();
   if(!self->conn_queue) { worker_free(self); return NULL; }

   self->base = event_base_new();
   if(!self->base) { worker_free(self); return NULL; }

   self->tid = tid++;
   self->acceptor_cb = acceptor_cb;
   self->notify_receive_fd = fds[0];
   self->notify_send_fd = fds[1];

   evutil_make_socket_nonblocking(self->notify_receive_fd);
   event_set(&self->notify_event, self->notify_receive_fd, EV_READ|EV_PERSIST, worker_socket_accepted_cb, self);
   event_base_set(self->base, &self->notify_event);
   assert(event_add(&self->notify_event, 0) != -1);

   pthread_create(&self->thread, NULL, worker, self);
   return self;
}

void worker_free(worker_t *self) {
   if(self->conn_queue) g_queue_free(self->conn_queue);
   if(self->base) event_base_free(self->base);
   free(self);
}

static void *worker(void *ptr) {
   worker_t *self = ptr;
   printf("Worker %d started\n", self->tid);
   event_base_dispatch(self->base);
   event_base_free(self->base);
   printf("Worker exited\n");
}

static void worker_socket_accepted_cb(int pipefd, short which, void *arg) {
   worker_t *self = arg;
   char cmd;
   read(self->notify_receive_fd, &cmd, 1);

   pthread_mutex_lock(&self->conn_queue_mutex);
   int fd = GPOINTER_TO_INT(g_queue_pop_head(self->conn_queue));
   pthread_mutex_unlock(&self->conn_queue_mutex);

   if(!fd) {
      printf("Notification received but conn_queue is empty\n");
      return;
   }

   evutil_make_socket_nonblocking(fd);
   fd_setcloseonexec(fd);
   self->acceptor_cb(self, fd);
}
