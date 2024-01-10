#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "asserts.h"
#include "../fcgi_parser.h"
#include "../fcgi_params_parser.h"

char input[] = "\x01\x01\x00\x01\x00\x08\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x01\x04\x00\x01\x01\xf4\x04\x00\x09\x00PATH_INFO\x0f\x17SCRIPT_FILENAME/var/www/html/test.fcgi\x0c\x00QUERY_STRING\x0e\x03REQUEST_METHODGET\x0c\x00""CONTENT_TYPE\x0e\x00""CONTENT_LENGTH\x0b\x0aSCRIPT_NAME/test.fcgi\x0b\x0aREQUEST_URI/test.fcgi\x0c\x0a""DOCUMENT_URI/test.fcgi\x0d\x0d""DOCUMENT_ROOT/var/www/html\x0f\x08SERVER_PROTOCOLHTTP/1.1\x0e\x04REQUEST_SCHEMEhttp\x11\x07GATEWAY_INTERFACECGI/1.1\x0f\x0cSERVER_SOFTWAREnginx/1.18.0\x0b\x09REMOTE_ADDR127.0.0.1\x0b\x05REMOTE_PORT44328\x0b\x00REMOTE_USER\x0b\x09SERVER_ADDR127.0.0.1\x0b\x02SERVER_PORT80\x0b\x01SERVER_NAME_\x0f\x03REDIRECT_STATUS200\x09\x09HTTP_HOSTlocalhost\x0f\x0bHTTP_USER_AGENTcurl/7.74.0\x0b\x03HTTP_ACCEPT*/*\x00\x00\x00\x00\x01\x04\x00\x01\x00\x00\x00\x00\x01\x05\x00\x01\x00\x00\x00\x00";

#define FCGI_PARAMS              4

void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   if(hdr->type == FCGI_PARAMS) {
      fcgi_params_parser_t *params_parser = (fcgi_params_parser_t*) userdata;
      fcgi_params_parser_write(params_parser, data, hdr->contentLength);
   }
}

void onfcgiparamreceived(const char *key, const char *value, void *userdata) {
   if(strcasecmp(key, "SCRIPT_FILENAME") == 0) {
      strcpy(userdata, value);
   }
}

int test_parse_script_filename() {
   fcgi_parser_t *fcgi_parser = fcgi_parser_new();
   fcgi_parser->callback = onfcgimessage;

   fcgi_params_parser_t *params_parser = fcgi_params_parser_new(4096);
   params_parser->callback = onfcgiparamreceived;

   char script_filename[4096];
   fcgi_parser->userdata = params_parser;
   params_parser->userdata = script_filename;

   fcgi_parser_write(fcgi_parser, input, sizeof(input) - 1);
   fcgi_params_parser_free(params_parser);
   fcgi_parser_free(fcgi_parser);

   ASSERT_STR_EQUAL(script_filename, "/var/www/html/test.fcgi");
}
