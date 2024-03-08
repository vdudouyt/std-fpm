#include "fcgitypes.h"

const char *fcgitype_to_string(int type) {
   switch(type) {
	   case FCGI_BEGIN_REQUEST: return "FCGI_BEGIN_REQUEST";
	   case FCGI_ABORT_REQUEST: return "FCGI_ABORT_REQUEST";
	   case FCGI_END_REQUEST: return "FCGI_END_REQUEST";
	   case FCGI_PARAMS: return "FCGI_PARAMS";
	   case FCGI_STDIN: return "FCGI_STDIN";
	   case FCGI_STDOUT: return "FCGI_STDOUT";
	   case FCGI_STDERR: return "FCGI_STDERR";
	   case FCGI_DATA: return "FCGI_DATA";
	   case FCGI_GET_VALUES: return "FCGI_GET_VALUES";
	   case FCGI_GET_VALUES_RESULT: return "FCGI_GET_VALUES_RESULT";
	   case FCGI_UNKNOWN_TYPE: return "FCGI_UNKNOWN_TYPE";
      default: return "UNKNOWN";
   }
}
