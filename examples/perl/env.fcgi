#!/usr/bin/perl
use strict;
use CGI::Fast;
use Data::Dumper;

while (my $q = CGI::Fast->new) {
   print "Content-type: text/plain\n\n";
   print Dumper(\%ENV);
}
