GNUMAKE = /usr/local/opt/make/libexec/gnubin/make
ifneq ($(wildcard $(GNUMAKE)),)
MAKE := $(GNUMAKE)
endif
MAKEFLAGS += --no-print-directory

PAS = $(patsubst %/Makefile,%,$(wildcard pa*/Makefile))
SORTED_PAS = $(shell printf '%s\n' $(PAS) | sort -t a -k 2,2n)
TEST_THROUGH_TARGETS = $(addprefix test-through-,$(SORTED_PAS))
DEV_BUILD_LOCK = obj/.dev-build.lock

.PHONY: all build test $(PAS) $(TEST_THROUGH_TARGETS)

all: build

build:
	@mkdir -p obj
	@lockdir=$(DEV_BUILD_LOCK); \
	while ! mkdir $$lockdir 2>/dev/null; do sleep 1; done; \
	trap 'rmdir "$$lockdir"' EXIT HUP INT TERM; \
	$(MAKE) -s -C dev

test: build
	@for dir in $(SORTED_PAS); do \
		echo "===== $$dir ====="; \
		$(MAKE) -C $$dir CPGM_SKIP_DEV_REBUILD=1 test || exit 1; \
	done
	@echo "===== ALL TESTS PASSED SUCCESSFULLY! ====="

$(PAS):
	$(MAKE) build
	$(MAKE) -C $@ CPGM_SKIP_DEV_REBUILD=1 test

$(TEST_THROUGH_TARGETS): build
	@target=$(@:test-through-%=%); \
	for dir in $(SORTED_PAS); do \
		echo "===== $$dir ====="; \
		$(MAKE) -C $$dir CPGM_SKIP_DEV_REBUILD=1 test || exit 1; \
		if [ "$$dir" = "$$target" ]; then break; fi; \
	done
	@echo "===== ALL TESTS PASSED SUCCESSFULLY! ====="
