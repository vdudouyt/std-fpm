#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <uv.h>
#include <assert.h>

#include "log.h"
#include "debug.h"
#include "conn.h"
#include "fcgi_parser.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "fcgi_writer.h"
#include "config.h"
#include "events.h"

static stdfpm_config_t *cfg = NULL;

#define EXIT_WITH_ERROR(...) { log_write(__VA_ARGS__); exit(-1); }
#define READ_RESUME(pipe) uv_read_start((uv_stream_t*) (pipe), stdfpm_alloc_buffer, stdfpm_read_completed_cb);

static int stdfpm_create_listening_socket(const char *sock_path) {
   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   if(listen_sock == -1) perror("[main] couldn't create a listener socket");

   struct sockaddr_un s_un;
   s_un.sun_family = AF_UNIX;
   strcpy(s_un.sun_path, sock_path);

   if(connect(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) == -1) {
      unlink(s_un.sun_path); // Socket is in use, prepare for binding
   }

   if(bind(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) == -1) {
      perror("[main] failed to bind a unix domain socket");
   }

   chmod(s_un.sun_path, 0777);
   if(listen(listen_sock, 1024) == -1) {
      perror("[main] failed to listen a unix domain socket");
   }

   return listen_sock;
}

static void stdfpm_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void stdfpm_read_completed_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);

static void stdfpm_onconnect(uv_stream_t *stream, int status) {
   DEBUG("stdfpm_onconnect()");
   assert(status == 0);
   uv_pipe_t *client = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
   assert(client);
   conn_t *conn = fd_new_client_conn(client);
   assert(conn);
   uv_handle_set_data((uv_handle_t *) client, conn);
   uv_pipe_init(stream->loop, client, 0);

   if(uv_accept(stream, (uv_stream_t*) client) == 0) {
      DEBUG("Socket accepted");
      uv_read_start((uv_stream_t*)client, stdfpm_alloc_buffer, stdfpm_read_completed_cb);
   } else {
      log_write("Accept failed");
      uv_close((uv_handle_t*) client, NULL);
   }
}

static void stdfpm_ondisconnect(uv_handle_t *uvhandle) {
   conn_t *conn = uv_handle_get_data(uvhandle);
   DEBUG("[%s] stdfpm_ondisconnect()", conn->name);
   if(conn->pairedWith) {
      if(!conn->pairedWith->pendingWrites) uv_close((uv_handle_t*) conn->pairedWith->pipe, stdfpm_ondisconnect);
      conn->pairedWith->pairedWith = NULL;
   }
   if(conn->type == STDFPM_FCGI_PROCESS) {
      pool_return_process(conn->process);
   }
   free(conn->pipe);
   free(conn);
}

static void stdfpm_onconnecterror(uv_handle_t *uvhandle) {
   free(uvhandle);
}

static void stdfpm_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   buf->base = (char*) malloc(4096);
   buf->len = 4096;
   assert(buf->base);
}

void stdfpm_onupstream_connect(uv_connect_t *req, int status);

static void fcgi_pair_with_process(conn_t *client, const char *script_filename) {
   DEBUG("fcgi_pair_with_process(%s, %s)", client->name, script_filename);

   fcgi_process_t *proc = pool_borrow_process(script_filename);
   client->probeMode = RETRY_ON_FAILURE;

   if(!proc) {
      static unsigned int ctr = 0;
      char pool_path[] = "/tmp/pool";
      char socket_path[4096];
      ctr++;
      snprintf(socket_path, sizeof(socket_path), "%s/stdfpm-%d.sock", pool_path, ctr);

      proc = fcgi_spawn(socket_path, script_filename);
      client->probeMode = CLOSE_ON_FAILURE;
   }

   if(!proc) {
      log_write("Failed to spawn a process: %s", script_filename);
      return;
   }

   if(client->process) free(client->process); // previous retry
   client->process = proc;

   DEBUG("Connecting to %s", proc->s_un.sun_path);
   uv_pipe_t *processConnHandle = malloc(sizeof(uv_pipe_t));
   uv_pipe_init(uv_default_loop(), processConnHandle, 0);
   uv_connect_t *processConnRequest = (uv_connect_t*) malloc(sizeof(uv_connect_t));
   uv_handle_set_data((uv_handle_t *) processConnRequest, client);
   uv_pipe_connect(processConnRequest, processConnHandle, proc->s_un.sun_path, stdfpm_onupstream_connect);
}

static void stdfpm_write_completed_cb(uv_write_t *req, int status) {
   conn_t *conn = uv_handle_get_data((uv_handle_t *) req->handle);
   DEBUG("stdfpm_write_completed_cb(status = %d)", status);
   conn->pendingWrites--;
   DEBUG("pendingWrites = %d", conn->pendingWrites);
   free(req);

   if(!conn->pendingWrites && !conn->pairedWith) {
      uv_close((uv_handle_t *) req->handle, stdfpm_ondisconnect);
   }

   READ_RESUME(conn->pairedWith->pipe);
}

void stdfpm_onupstream_connect(uv_connect_t *processConnRequest, int status) {
   conn_t *clientConn = uv_handle_get_data((uv_handle_t *) processConnRequest);
   fcgi_process_t *proc = clientConn->process;
   uv_stream_t *processConnHandle = processConnRequest->handle;

   if(status == 0) {
      DEBUG("connected to fastcgi process: %s:%s",
         proc->s_un.sun_path, proc->filepath);

      conn_t *processConn = fd_new_process_conn(proc, (uv_pipe_t*) processConnHandle);
      DEBUG("writing %d of stored bytes from %s to %s", clientConn->storedBuf.len, clientConn->name, processConn->name);
      uv_write_t *wreq = (uv_write_t *)malloc(sizeof(uv_write_t));
      uv_write((uv_write_t *)wreq, processConnHandle, &clientConn->storedBuf, 1, stdfpm_write_completed_cb);
      uv_read_stop((uv_stream_t*) clientConn->pipe);
      free(clientConn->storedBuf.base);

      uv_handle_set_data((uv_handle_t *) processConnHandle, processConn);
      uv_read_start(processConnHandle, stdfpm_alloc_buffer, stdfpm_read_completed_cb);
      processConn->pairedWith = clientConn;
      clientConn->pairedWith = processConn;
      processConn->pendingWrites++;
   } else if(clientConn->probeMode == RETRY_ON_FAILURE) {
      DEBUG("[%s] failed while connecting to fastcgi process, trying the next: %s:%s",
         clientConn->name, proc->s_un.sun_path, proc->filepath);
      fcgi_pair_with_process(clientConn, proc->filepath); // frees proc automatically
      uv_close((uv_handle_t*) processConnHandle, stdfpm_onconnecterror);
   } else if(clientConn->probeMode == CLOSE_ON_FAILURE) {
      log_write("execve() succeeded, yet failed while connecting to fastcgi process. Terminating client's connection: %s",
         proc->filepath);
      free(proc);
      free(clientConn->storedBuf.base);
      uv_close((uv_handle_t*) processConnHandle, stdfpm_onconnecterror);
      uv_close((uv_handle_t*) clientConn->pipe, stdfpm_ondisconnect);
   }

   free(processConnRequest);
}

static void stdfpm_read_completed_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
   conn_t *conn = uv_handle_get_data((uv_handle_t *) client);

   if(nread < 0) {
      nread == UV_EOF ? DEBUG("EOF reached") : log_write("Read error %s", uv_err_name(nread));
      uv_close((uv_handle_t*) client, stdfpm_ondisconnect);
   }

   if(nread <= 0) {
      if (buf->base) {
         DEBUG("freeing buf (1)");
         free(buf->base);
      }
      return;
   }

   #ifdef DEBUG_LOG
   char escaped_data[4*65536+1];
   escape(escaped_data, buf->base, nread);
   DEBUG("[%s] message size: %ld", conn->name, nread);
   DEBUG("[%s] message content: \"%s\"", conn->name,  escaped_data);
   #endif

   if(!conn) {
      log_write("uv_handle_get_data() failed");
      return;
   }

   if(conn->type == STDFPM_FCGI_CLIENT && !conn->pairedWith) {
      fcgi_parse(&conn->fcgiParser, buf->base, nread);
      char *script_filename = fcgi_get_script_filename(&conn->fcgiParser);

      conn->storedBuf.base = buf->base; // TODO: append/realloc
      conn->storedBuf.len = nread;

      if(script_filename) {
         fcgi_pair_with_process(conn, script_filename);
      }
   } else if(conn->pairedWith) {
      uv_buf_t wrbuf = { .base = buf->base, .len = nread };
      uv_write_t *wreq = (uv_write_t *)malloc(sizeof(uv_write_t));
      conn->pairedWith->pendingWrites++;
      DEBUG("pumping %d bytes from %s to %s", nread, conn->name, conn->pairedWith->name);
      uv_write((uv_write_t *)wreq, (uv_stream_t *) conn->pairedWith->pipe, &wrbuf, 1, stdfpm_write_completed_cb);
      uv_read_stop(client);
      free(buf->base);
   }
}

int main(int argc, char **argv) {
   log_set_echo(true);
   pool_init();
   uv_loop_t *loop = uv_default_loop();

   char sockpath[] = "/tmp/std-fpm.sock";
   uv_pipe_t pipe;
   uv_pipe_init(loop, &pipe, 0);
   unlink(sockpath);
   uv_pipe_bind(&pipe, sockpath);
   chmod(sockpath, 0777);

   uv_listen((uv_stream_t *)&pipe, 1024, stdfpm_onconnect);
   uv_run(loop, UV_RUN_DEFAULT);
}
