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

sub getdata
{
	my $file = shift @_;

	my $data = `cat $file`;
	chomp($data);
	return $data;
}

for my $test (@tests)
{
	print "\n$test: " if $verbose;

	my $testbase = $test;
	$testbase =~ s/\.t\.1$//;

	my $ref = "$testbase.$ref_suffix";
	my $ref_impl_exit_status = "$ref.impl.exit_status";
	my $ref_program_exit_status = "$ref.program.exit_status";
	my $ref_program_stdout = "$ref.program.stdout";

	my $my = "$testbase.$my_suffix";
	my $my_impl_exit_status = "$my.impl.exit_status";
	my $my_program_exit_status = "$my.program.exit_status";
	my $my_program_stdout = "$my.program.stdout";

	if (getdata($ref_impl_exit_status) ne getdata($my_impl_exit_status))
	{
		print fail_prefix();
		print "$test: ERROR: implementations exit statuses do not match (.impl.exit_status)\n\n";
		print rerun_hint();
		print "TEST FAIL\n";
		exit(1);
	}
	elsif (getdata($ref_impl_exit_status) ne "0")
	{
		$npass++;
		print "PASS\n\n" if $verbose;
	}
	elsif ((getdata($ref_program_exit_status) eq getdata($my_program_exit_status)) and
		(getdata($ref_program_stdout) eq getdata($my_program_stdout)))
	{
		$npass++;
		print "PASS\n\n" if $verbose;
	}
	else
	{
		print fail_prefix();
		print "$test: ERROR: generated programs do not match in exit status and/or output\n\n";
		print rerun_hint();
		print "To compare generated program output:\n\n    \$ diff " . rooted_path($ref_program_stdout) . " " . rooted_path($my_program_stdout) . "\n\n";
		print "TEST FAIL\n";
		exit(1);
	}
}

print "$tests: PASS ($npass/$suite_total)\n";
