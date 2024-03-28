#!/usr/bin/env luajit
local fcgi = require("fcgi")

-- Usage example:
-- $ curl -X PUT 'http://localhost/put_file.fcgi' --data-binary "@/tmp/1mb.bin"
-- OK

while fcgi.accept() do
   local f = io.open("/tmp/file.bin", "wb")
   while true do
      local next_chunk = fcgi.read(8192)
      if next_chunk == "" then
         break
      end
      f:write(next_chunk)
   end
   f:close()
   fcgi.print("Content-type: text/html\n\nOK")
end
