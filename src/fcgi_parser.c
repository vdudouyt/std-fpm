#include "fcgi_parser.h"
#include <stdio.h>
#include <string.h>

#define FCGI_PARAMS              4
#define BE32(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

static inline void parse_header(fcgi_request_parser_t *ctx, uint8_t c);
static inline void parse_params(fcgi_params_parser_t *ctx, uint8_t c);

void fcgi_parser_init(fcgi_parser_t *ctx) {
   memset(ctx, 0, sizeof(fcgi_parser_t));
}

void fcgi_parse(fcgi_parser_t *ctx, const char *buf, size_t len) {
   fcgi_request_parser_t *requestParser = &ctx->requestParser;
   for(unsigned int i = 0; i < len; i++) {
      if(requestParser->pos >= 8 && requestParser->pos >= 8 + requestParser->length + requestParser->paddingLength) {
         requestParser->pos = 0;
      }

      if(requestParser->pos < 8) {
         parse_header(requestParser, buf[i]);
      } else if(requestParser->type == FCGI_PARAMS) {
         parse_params(&ctx->paramsParser, buf[i]);
      }

      requestParser->pos++;
      if(ctx->paramsParser.gotScriptFilename) break;
   }
}

char *fcgi_get_script_filename(fcgi_parser_t *ctx) {
   return ctx->paramsParser.gotScriptFilename ? &ctx->paramsParser.buf[15] : NULL;
}

static inline void parse_header(fcgi_request_parser_t *ctx, uint8_t c) {
   switch(ctx->pos) {
      case 1: ctx->type = c; break;
      case 4: ctx->length = c << 8; break;
      case 5: ctx->length |= c; break;
      case 6: ctx->paddingLength = c; break;
   }
}

static inline void parse_params(fcgi_params_parser_t *ctx, uint8_t c) {
   static unsigned char key[] = "SCRIPT_FILENAME";
   if(ctx->pos < sizeof(ctx->buf) - 2) {
      ctx->buf[ctx->pos] = c;
      ctx->pos++;
   }

   const bool got_uint8  = ctx->pos == 1 && !(ctx->buf[0] & 0x80);
   const bool got_uint32 = ctx->pos == 4 && (ctx->buf[0] & 0x80);

   if(ctx->state <= READ_VALUE_LENGTH && (got_uint8 || got_uint32)) {
      unsigned int *out = ctx->state == READ_VALUE_LENGTH ? &ctx->valueLength : &ctx->keyLength;
      *out = got_uint32 ? BE32(ctx->buf[0] & 0x7F, ctx->buf[1], ctx->buf[2], ctx->buf[3]) : ctx->buf[0];
      ctx->state++;
      ctx->pos = 0;
   }

   if(ctx->state == READ_PARAM && ctx->pos >= ctx->keyLength + ctx->valueLength) {
      ctx->buf[ctx->pos] = '\0';
      ctx->state = ctx->pos = 0;

      if(ctx->keyLength == 15 && !memcmp(ctx->buf, "SCRIPT_FILENAME", 15)) {
         ctx->gotScriptFilename = true;
         return;
      }
   }
}
