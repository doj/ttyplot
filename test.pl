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
	if ($x > 3)
	{
	    print " cos ";
	    print cos($x*0.9)*80.0;
	}
	if ($x > 6)
	{
	    print " misc   ";
	    print cos($x*0.13+2)*50.0;
	}
	if ($x > 9)
	{
	    print " a 20 b -30 ca 3 cb 5 cc 6 f 7 g 9";
	}
	print "\n";
	usleep(50000);
    }
}

if ($ARGV[0] eq '-r')
{
    my $x = 0;
    my $y = 1;
    while(1)
    {
	$x += 0.2;
	$y *= 1.02;
	print "lin $x exp $y\n";
	usleep(50000);
    }
}

if ($ARGV[0] eq '--rateoverflow')
{
    # simulate 32 bit unsigned overflow
    if (0)
    {
	my $x = 0xfffffff0;
	while(1)
	{
	    print "$x ";
	    usleep(50000);
	    $x = 0 if ++$x > 0xffffffff;
	}
    }
    # simulate 31 bit unsigned overflow
    else
    {
	my $x = 0x7ffffff0;
	while(1)
	{
	    print "$x ";
	    usleep(50000);
	    $x = 0 if ++$x > 0x7fffffff;
	}
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
