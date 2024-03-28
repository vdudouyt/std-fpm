#!/usr/bin/env luajit
local fcgi = require("fcgi")

-- Usage example:
-- $ curl -X PUT 'http://localhost/test_put.fcgi' -d "John Doe"
-- Hello, John Doe!

while fcgi.accept() do
   local name = fcgi.read()
   fcgi.print("Content-type: text/html\n\n")
   fcgi.print(string.format("Hello, %s!", name))
end
