#!/usr/bin/perl

use strict;
use warnings;

if (scalar(@ARGV) != 3)
{
	die "Usage: test.pl <app> <suffix> <testlocation>";
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

	my $test_out = $test;
	$test_out =~ s/\.t\.1$/\.$suffix/;
	my $test_base = $test;
	$test_base =~ s/\.t\.1$/\.t/;

	my $command = "scripts/run_one_test.sh $app $test_out $test_base";
	my $sys_ret = system($command);
	if ($sys_ret == 0)
	{
		system("echo EXIT_SUCCESS > $test_out.exit_status");
	}
	else
	{
		system("echo EXIT_FAILURE > $test_out.exit_status");
	}
}
