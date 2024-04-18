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

   uv_timer_t tim1;
   uv_timer_init(loop, &tim1);
   uv_timer_start(&tim1, pool_rip_idling, 0, 1000);

   uv_listen((uv_stream_t *)&pipe, 1024, stdfpm_conn_received_cb);
   uv_run(loop, UV_RUN_DEFAULT);
}
