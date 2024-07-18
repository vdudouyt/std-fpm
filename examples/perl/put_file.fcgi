#!/usr/bin/perl
use strict;
use CGI::Fast;
use IO::Handle;

my $handle = IO::Handle->new;
CGI::Fast->file_handles({
    fcgi_input_file_handle  => $handle,
});

while (my $q = CGI::Fast->new) {
   binmode($handle);
   open(FILE, "/tmp/1.bin");
   my ($buf, $bytes_read);
   
   while(read(STDIN, $buf, 1024 * 64)) {
      print FH $buf;
      $bytes_read += length($buf);
   }

   close(FILE);
   print "Content-type: text/html\n\nOK\n";
}
