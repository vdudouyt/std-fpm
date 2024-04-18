#pragma once
#include "conn.h"

void stdfpm_connect_process(conn_t *client, const char *script_filename);
void stdfpm_onupstream_connect(uv_connect_t *processConnRequest, int status);
void stdfpm_onconnecterror(uv_handle_t *uvhandle);
