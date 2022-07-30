#!/usr/bin/perl
# create test data for the ttyplot program.

use strict;
use Time::HiRes qw(usleep);

$| = 1; # disable STDOUT buffer

if ($ARGV[0] eq '-2')
{
    my $x = 0;
    while(1)
    {
	$x += 0.2;
	print sin($x)*100.0;
	print ' ';
	print cos($x*0.9)*80.0;
	print ' ';
	usleep(50000);
    }
}

if ($ARGV[0] eq '-k')
{
    my $x = 0;
    while(1)
    {
	$x += 0.2;
	print "sin ";
	print sin($x)*100.0;
	print " cos ";
	print cos($x*0.9)*80.0;
	print " misc   ";
	print cos($x*0.13+2)*50.0;
	print " a 20 b -30 ca 3 cb 5 cc 6 f 7 g 9\n ";
	usleep(50000);
    }
}

my $x = 0;
while(1)
{
    $x += 0.2;
    print sin($x)*100.0;
    print ' ';
    usleep(50000);
}
