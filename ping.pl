#!/usr/bin/perl
#
# ping multiple hosts and produce key/value pairs for ttyplot.
#
# usage: ping.pl <hostname>+

use strict;
use FileHandle;
use IO::Select;

my %p;
foreach my $n (@ARGV)
{
    $p{$n} = FileHandle->new("ping $n |") or die "could not ping $n : $!";
}

$| = 1; # STDOUT autoflush
while(1)
{
    my $os = ''; # output string
    # wait up to 1.1 seconds for the first line read,
    # then don't wait any more and read all lines available.
    my $wait = 1.1;
    foreach my $n (@ARGV)
    {
	$_ = '';
	my $fh = $p{$n};
	# read all lines available
	my $select = IO::Select->new();
	$select->add($fh);
	while($select->can_read($wait))
	{
	    $_ = <$fh>;
	    $wait = 0;
	}
	$os .= "$n $1 " if /time=(.+) ms/;
    }
    print "$os\n" if length($os);
}
