#!/usr/bin/env luajit
local fcgi = require("fcgi")

local i = 1

while fcgi.accept() do
   fcgi.print("Content-type: text/html\n\n")
   fcgi.print(string.format("Hello! You have requested this page %d time(s)<br>\n", i))
   fcgi.print("Your IP address is: " .. os.getenv("REMOTE_ADDR"))
   i = i + 1
end
