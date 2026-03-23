#!/usr/bin/perl

use strict;
use warnings;
use Cwd qw(getcwd);
use File::Basename qw(basename dirname);

if (scalar(@ARGV) != 3)
{
	die "Usage: compare_results.pl <ref_suffix> <my_suffix> <testlocation>";
}

my $ref_suffix = $ARGV[0];
my $my_suffix = $ARGV[1];
my $tests = $ARGV[2];
my $verbose = $ENV{VERBOSE} || $ENV{CPGM_TEST_VERBOSE};
my $cwd = getcwd();
my $assignment = basename($cwd);
my $repo_root = dirname($cwd);

my @tests = grep { m/\.t\.1$/ } sort split(/\s+/, `find $tests -type f`);
my $suite_total = scalar(@tests);

my $npass = 0;

sub rooted_path
{
	my ($path) = @_;
	return $path =~ m{^/} ? $path : "$cwd/$path";
}

sub fail_prefix
{
	return "$tests: FAIL after $npass/$suite_total passed\n";
}

sub rerun_hint
{
	return "To rerun this assignment with per-test output from the repo root:\n\n    \$ cd $repo_root && VERBOSE=1 make $assignment\n\n";
}

for my $test (@tests)
{
	print "\n$test: " if $verbose;

	my $testbase = $test;
	$testbase =~ s/\.t\.1$//;

	my $reftest = "$testbase.$ref_suffix";
	my $reftest_exit_status = "$reftest.exit_status";
	my $mytest = "$testbase.$my_suffix";
	my $mytest_exit_status = "$mytest.exit_status";

	my $reftest_data = `cat $reftest`;
	my $reftest_exit_status_data = `cat $reftest_exit_status`;
	my $mytest_data = `cat $mytest`;
	my $mytest_exit_status_data = `cat $mytest_exit_status`;

	chomp($reftest_data);
	chomp($reftest_exit_status_data);
	chomp($mytest_data);
	chomp($mytest_exit_status_data);

	if ($reftest_exit_status_data ne $mytest_exit_status_data)
	{
		print fail_prefix();
		print "$test: ERROR: Expected $reftest_exit_status_data, got $mytest_exit_status_data\n\n";
		print rerun_hint();
		print "TEST FAIL\n";
		exit(1);
	}
	elsif ($reftest_exit_status_data =~ /EXIT_FAILURE/ or ($reftest_data eq $mytest_data))
	{
		$npass++;
		print "PASS\n\n" if $verbose;
	}
	else
	{
		print fail_prefix();
		print "$test: ERROR: Output does not match reference implementation\n\n";
		print rerun_hint();
		print "To see input file:\n\n    \$ cat " . rooted_path($test) . "\n\n";
		print "To see hex dump of input:\n\n    \$ xxd " . rooted_path($test) . "\n\n";
		print "To see the differences of output:\n\n    \$ diff " . rooted_path($reftest) . " " . rooted_path($mytest) . "\n\n";
		print "TEST FAIL\n";
		exit(1);
	}
}

print "$tests: PASS ($npass/$suite_total)\n";
