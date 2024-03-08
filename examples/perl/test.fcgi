#!/usr/bin/perl
use strict;
use CGI::Fast;

my $i = 1;
while (my $q = CGI::Fast->new) {
   print "Content-type: text/html\n\nHello! You have requested this page $i time(s)\n";
   $i++;
}
