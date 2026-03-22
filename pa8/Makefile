CXX ?= g++
PROG = nsinit
COURSE_TEST_ROOT = $(wildcard course/pa8)
DEV_BUILD_LOCK = ../obj/.dev-build.lock

.PHONY: all test ref-test dev-shared-target run check

all: dev-shared-target

ifeq ($(COURSE_TEST_ROOT),)
test: dev-shared-target
	@scripts/run_all_tests.pl ../dev/$(PROG) my tests
	@scripts/compare_results.pl ref my tests
else
test: dev-shared-target
	@scripts/run_all_tests.pl ../dev/$(PROG) my tests
	@scripts/compare_results.pl ref my tests
	@scripts/run_all_tests.pl ../dev/$(PROG) my $(COURSE_TEST_ROOT)
	@scripts/compare_results.pl ref my $(COURSE_TEST_ROOT)
endif

dev-shared-target:
	@if [ "$(CPGM_SKIP_DEV_REBUILD)" = "1" ]; then \
		true; \
	else \
		mkdir -p ../obj; \
		lockdir=$(DEV_BUILD_LOCK); \
		while ! mkdir $$lockdir 2>/dev/null; do sleep 1; done; \
		trap 'rmdir "$$lockdir"' EXIT HUP INT TERM; \
		$(MAKE) -s -C ../dev $(PROG); \
	fi

ref-test:
	@scripts/run_all_tests.pl nsinit-ref ref tests
	@scripts/run_all_tests.pl nsinit-other other tests
	-rm -f tests/*.other
	@scripts/compare_exit_status.pl other ref

run: dev-shared-target
	@if [ -z "$(INPUT)" ]; then \
		echo "Usage: make run INPUT=<input-file-or-glob> [RUN_OUTPUT=/tmp/nsinit.out]"; \
		exit 1; \
	fi
	@outfile=$${RUN_OUTPUT:-/tmp/$(PROG).out}; \
	rm -f "$$outfile"; \
	../dev/$(PROG) -o "$$outfile" $(INPUT); \
	xxd "$$outfile"

check: dev-shared-target
	@if [ -z "$(TEST)" ]; then \
		echo "Usage: make check TEST=<checked-in-test-file>"; \
		exit 1; \
	fi
	@scripts/run_all_tests.pl ../dev/$(PROG) check "$(TEST)"
	@scripts/compare_results.pl ref check "$(TEST)"
