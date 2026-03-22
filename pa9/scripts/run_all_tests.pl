#!/usr/bin/perl

use strict;
use warnings;

if (scalar(@ARGV) != 3)
{
	die "Usage: run_all_tests.pl <app> <suffix>";
}

my $app = $ARGV[0];
my $suffix = $ARGV[1];
my $tests = $ARGV[2];
my $verbose = $ENV{VERBOSE} || $ENV{CPGM_TEST_VERBOSE};

my @tests = grep { m/\.t\.1$/ } sort split(/\s+/, `find $tests -type f`);
my $ntests = scalar(@tests);

if (!$verbose)
{
	print "$tests: running $ntests test";
	print "s" if $ntests != 1;
	print "\n";
}

for my $test (@tests)
{
	print "Running $test...\n" if $verbose;

	my $test_base = $test;
	$test_base =~ s/\.t\.1$//;

	my $command = "scripts/run_one_test.sh $app $suffix $test_base";
	print "$command\n" if $verbose;
	system($command);
}
