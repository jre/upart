#!/usr/bin/perl

use strict;

my $off;
my $sparse = 0;
my $done = 0;

sub fail
{
    print STDERR "error on line $.: ", @_, "\n";
    exit 1;
}

while(<STDIN>)
{
    fail("expected end of input") if $done;
    chomp;

    if('*' eq $_)
    {
        fail("found adjacent * lines") if $sparse;
        fail("found * line before first normal line") unless defined $off;
        $sparse = 1;
        next;
    }

    unless(/^([0-9a-fA-F]{8})(?:$|\s+((?:[0-9a-fA-F]{2}\s+)+)\|)/)
    {
        fail("unknown line format") if defined $off;
        next;
    }

    my $next = hex($1);
    my @bytes;

    if(defined $2)
    {
        @bytes = split(' ', $2);
    }
    else
    {
        $done = 1;
    }

    if($sparse)
    {
        print "\0" x ($next - $off);
        $sparse = 0;
    }

    print join('', map {chr hex} @bytes);
    $off = $next + scalar(@bytes);;
}
