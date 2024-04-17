#include "events.h"
#include "log.h"
#include "debug.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "connect_process.h"
#include "assert.h"

#define READ_RESUME(pipe) uv_read_start((uv_stream_t*) (pipe), stdfpm_alloc_buffer, stdfpm_read_completed_cb);

void stdfpm_conn_received_cb(uv_stream_t *stream, int status) {
   DEBUG("stdfpm_conn_received_cb()");
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

void stdfpm_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
   buf->base = (char*) malloc (4096);
   buf->len = 4096;
   assert(buf->base);
}

void stdfpm_read_completed_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
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
         stdfpm_connect_process(conn, script_filename);
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

void stdfpm_write_completed_cb(uv_write_t *req, int status) {
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

void stdfpm_ondisconnect(uv_handle_t *uvhandle) {
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
