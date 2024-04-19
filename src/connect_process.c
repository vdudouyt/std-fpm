#include "connect_process.h"
#include "events.h"
#include "log.h"
#include "debug.h"
#include "process_pool.h"

void stdfpm_connect_process(conn_t *client, const char *script_filename) {
   DEBUG("stdfpm_connect_process(%s, %s)", client->name, script_filename);
   fcgi_process_t *proc = pool_borrow_process(script_filename);
   client->probeMode = proc ? RETRY_ON_FAILURE : CLOSE_ON_FAILURE;

   if(!proc) {
      static unsigned int ctr = 0;
      char socket_path[4096];
      ctr++;
      snprintf(socket_path, sizeof(socket_path), "%s/stdfpm-%d.sock", get_config()->pool, ctr);
      proc = fcgi_spawn(socket_path, script_filename);
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
      stdfpm_connect_process(clientConn, proc->filepath); // frees proc automatically
      if(!uv_is_closing((uv_handle_t *)processConnHandle)) uv_close((uv_handle_t*) processConnHandle, stdfpm_onconnecterror);
   } else if(clientConn->probeMode == CLOSE_ON_FAILURE) {
      log_write("execve() succeeded, yet failed while connecting to fastcgi process. Terminating client's connection: %s",
         proc->filepath);
      free(proc);
      free(clientConn->storedBuf.base);
      if(!uv_is_closing((uv_handle_t *)processConnHandle)) uv_close((uv_handle_t*) processConnHandle, stdfpm_onconnecterror);
      if(!uv_is_closing((uv_handle_t *)clientConn->pipe)) uv_close((uv_handle_t*) clientConn->pipe, stdfpm_ondisconnect);
   }

   free(processConnRequest);
}

void stdfpm_onconnecterror(uv_handle_t *uvhandle) {
   free(uvhandle);
}
