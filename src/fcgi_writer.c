#include "fcgi_writer.h"
#include "fcgitypes.h"
#include "debug.h"
#include "log.h"

void fcgi_write_buf(struct evbuffer *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength) {
   const unsigned char paddingLength = (4 - (contentLength % 4)) % 4;
   char padding[] = { 0, 0, 0, 0 };
   char hdr[8];
   hdr[0] = 0x01; // version
   hdr[1] = type;
   hdr[2] = (requestId & 0xff00) > 8;
   hdr[3] = requestId & 0xff;
   hdr[4] = (contentLength & 0xff00) > 8;
   hdr[5] = contentLength & 0xff;
   hdr[6] = paddingLength;
   hdr[7] = 0;    // reserved
   evbuffer_add(outBuf, hdr, sizeof(hdr));
   evbuffer_add(outBuf, content, contentLength);
   evbuffer_add(outBuf, padding, paddingLength);
}

void fcgi_send_response(fd_ctx_t *ctx, const char *response, size_t size) {
   DEBUG("fcgi_send_response");
   struct evbuffer *dst = bufferevent_get_output(ctx->bev);
   fcgi_write_buf(dst, 1, FCGI_STDOUT, response, size);
   fcgi_write_buf(dst, 1, FCGI_STDOUT, "", 0);
   fcgi_write_buf(dst, 1, FCGI_END_REQUEST, "\0\0\0\0\0\0\0\0", 8);
   ctx->closeAfterWrite = true;
}

void fcgi_serve_response(int listen_sock, const char *response, size_t size) {
   struct sockaddr_un client_sockaddr;
   unsigned int len = sizeof(client_sockaddr);

   int client_sock = accept(listen_sock, (struct sockaddr *) &client_sockaddr, &len);
   DEBUG("Accepted client sock: sck%d", client_sock);
   if(client_sock == -1) {
      log_write("[fastcgi spawner] socket accept failed, unable to report an error to std-fpm worker");
      return;
   }

   DEBUG("[fastcgi spawner] socket accepted");

   struct evbuffer *outBuf = evbuffer_new();

   if(!outBuf) {
      log_write("[fastcgi spawner] failed to allocate outBuf for error output");
      return;
   }

   fcgi_write_buf(outBuf, 1, FCGI_STDOUT, response, size);
   fcgi_write_buf(outBuf, 1, FCGI_STDOUT, "", 0);
   fcgi_write_buf(outBuf, 1, FCGI_END_REQUEST, "\0\0\0\0\0\0\0\0", 8);
   evbuffer_write(outBuf, client_sock);

   evbuffer_free(outBuf);
   close(client_sock);
}
