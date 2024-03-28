local ffi = require("ffi")
local clib = ffi.load("fcgi")
ffi.cdef[[

typedef struct {
    void *stdio_stream;
    void *fcgx_stream;
} FCGI_FILE;

extern  FCGI_FILE   _fcgi_sF[];
int FCGI_Accept();
ssize_t FCGI_fread(void *ptr, size_t size, size_t nmemb, FCGI_FILE *fp);
size_t FCGI_fwrite(void *ptr, size_t size, size_t nmemb, FCGI_FILE *fp);

]]

local FCGI = {}

function FCGI.accept()
   return clib.FCGI_Accept() >= 0
end

function FCGI.read(size)
   local buf_size = size or 16384
   local buf = ffi.new("uint8_t[?]", buf_size)
   if size ~= nil then
      local bytes_read = clib.FCGI_fread(buf, 1, buf_size, clib._fcgi_sF[0])
      return ffi.string(buf, bytes_read)
   else
      local ret = {}
      while true do
         local bytes_read = clib.FCGI_fread(buf, 1, buf_size, clib._fcgi_sF[0])
         if bytes_read <= 0 then
            return table.concat(ret)
         end
         table.insert(ret, ffi.string(buf, bytes_read))
      end
   end
end

function FCGI.print(s)
   clib.FCGI_fwrite(ffi.cast("void*", s), #s, 1, clib._fcgi_sF[1])
end

return FCGI
