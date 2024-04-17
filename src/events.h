#pragma once
#include "conn.h"

void stdfpm_conn_received_cb(uv_stream_t *stream, int status);
void stdfpm_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
void stdfpm_read_completed_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);
void stdfpm_write_completed_cb(uv_write_t *req, int status);
void stdfpm_ondisconnect(uv_handle_t *uvhandle);
