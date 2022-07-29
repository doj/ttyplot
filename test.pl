#!/usr/bin/perl
# create test data for the ttyplot program.

use strict;
use Time::HiRes qw(usleep);

$| = 1; # disable STDOUT buffer

my $x = 0;
while(1)
{
    $x += 0.03;
    print sin($x)*100.0;
    print ' ';
    usleep(100000);
}
