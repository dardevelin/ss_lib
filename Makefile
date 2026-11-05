# SS_Lib build. Every object (library, tests, examples, benchmarks) is
# compiled with the same FEATURES so callers and library always agree.

ifeq ($(origin CC),default)
    CC = gcc
endif

VERSION := $(shell scripts/version.sh)
MAJOR := $(firstword $(subst ., ,$(VERSION)))

CSTD ?= -std=c11
LIB_STD ?= $(CSTD)
OPT ?= -O2
WARN = -Wall -Wextra -pedantic
FEATURES ?= -DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1
IMPL ?= src/ss_lib.c
BUILD_DIR ?= build

CFLAGS = $(CSTD) $(WARN) $(OPT) -Iinclude $(FEATURES) $(EXTRA_CFLAGS)
LIB_CFLAGS = $(LIB_STD) $(WARN) $(OPT) -Iinclude $(FEATURES) $(EXTRA_CFLAGS) -fPIC
LDFLAGS = -pthread $(EXTRA_LDFLAGS)

# Sanitizers (make SANITIZER=address|thread|undefined|memory ...)
ifneq ($(SANITIZER),)
    EXTRA_CFLAGS += -fsanitize=$(SANITIZER) -fno-omit-frame-pointer -fno-sanitize-recover=all -g
    EXTRA_LDFLAGS += -fsanitize=$(SANITIZER)
    OPT = -O1
endif
ifeq ($(SANITIZER),memory)
    CC = clang
endif

# Coverage (make COVERAGE=1 ...)
ifeq ($(COVERAGE),1)
    EXTRA_CFLAGS += --coverage
    EXTRA_LDFLAGS += --coverage
    OPT = -O0
endif

# Shared library versioning follows semver: the major version is the ABI.
#   Linux: libss_lib.so -> libss_lib.so.MAJOR (soname) -> libss_lib.so.VERSION
#   macOS: libss_lib.dylib -> libss_lib.MAJOR.dylib -> libss_lib.VERSION.dylib
# On macOS the installed dylib is relinked with its absolute installed path as
# install name, so programs linked against it start without DYLD_* variables.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_EXT = dylib
    SHARED_REAL = libss_lib.$(VERSION).dylib
    SHARED_SONAME = libss_lib.$(MAJOR).dylib
    SHARED_FLAGS = -dynamiclib -current_version $(VERSION) -compatibility_version $(MAJOR).0.0
    SHARED_BUILD_ID = -install_name @rpath/$(SHARED_SONAME)
    INSTALL_SHARED = $(CC) $(SHARED_FLAGS) -install_name $(INSTALL_LIB_DIR)/$(SHARED_SONAME) \
        -o $(INSTALL_LIB_DIR)/$(SHARED_REAL) $(LIB_OBJ) $(LDFLAGS)
else ifeq ($(UNAME_S),Linux)
    SHARED_EXT = so
    SHARED_REAL = libss_lib.so.$(VERSION)
    SHARED_SONAME = libss_lib.so.$(MAJOR)
    SHARED_FLAGS = -shared -Wl,-soname,$(SHARED_SONAME)
else
    SHARED_EXT = so
    SHARED_REAL = libss_lib.so
    SHARED_FLAGS = -shared
endif
INSTALL_SHARED ?= install -m 755 $(BUILD_DIR)/$(SHARED_REAL) $(INSTALL_LIB_DIR)

LIB_OBJ = $(BUILD_DIR)/ss_lib.o
LIB_NAME = $(BUILD_DIR)/libss_lib.a
LIB_SO = $(BUILD_DIR)/libss_lib.$(SHARED_EXT)

TEST_BINS = $(BUILD_DIR)/test_ss_lib $(BUILD_DIR)/test_simple $(BUILD_DIR)/test_memory \
    $(BUILD_DIR)/test_isr
ifneq ($(OS),Windows_NT)
    TEST_BINS += $(BUILD_DIR)/test_threads
endif
# example_embedded needs the embedded configuration: see test_embedded
EXAMPLE_BINS = $(BUILD_DIR)/example_usage $(BUILD_DIR)/example_embedded_simple
BENCH_BIN = $(BUILD_DIR)/benchmark_ss_lib

.PHONY: all lib shared tests examples test test_embedded test-static test-minimal \
        test-c89 test-newest test-all test-asan test-tsan test-ubsan test-msan \
        test-valgrind coverage benchmark benchmark-all single-header \
        check-standards check-single-header check-version cppcheck release-notes install \
        check-install uninstall docs info help clean

all: lib tests examples

lib: $(LIB_NAME)
shared: $(LIB_SO)
tests: $(TEST_BINS)
examples: $(EXAMPLE_BINS)

$(BUILD_DIR):
	@mkdir -p $@

$(LIB_OBJ): $(IMPL) include/ss_lib.h include/ss_config.h | $(BUILD_DIR)
	$(CC) $(LIB_CFLAGS) -c $< -o $@

$(LIB_NAME): $(LIB_OBJ)
	ar rcs $@ $^

$(BUILD_DIR)/$(SHARED_REAL): $(LIB_OBJ)
	$(CC) $(SHARED_FLAGS) $(SHARED_BUILD_ID) -o $@ $^ $(LDFLAGS)

ifneq ($(SHARED_SONAME),)
$(LIB_SO): $(BUILD_DIR)/$(SHARED_REAL)
	ln -sf $(SHARED_REAL) $(BUILD_DIR)/$(SHARED_SONAME)
	ln -sf $(SHARED_SONAME) $@
endif

$(BUILD_DIR)/test_%: tests/test_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) $(LDFLAGS) -o $@

# Compiles the implementation into the test with a tracking allocator
$(BUILD_DIR)/test_memory: tests/test_memory.c $(IMPL) include/ss_lib.h include/ss_config.h | $(BUILD_DIR)
	$(CC) $(LIB_CFLAGS) -DSS_TEST_IMPL='"../$(IMPL)"' $< $(LDFLAGS) -o $@

# Compiles the implementation into the test with a hookable ISR critical section
$(BUILD_DIR)/test_isr: tests/test_isr.c $(IMPL) include/ss_lib.h include/ss_config.h | $(BUILD_DIR)
	$(CC) $(LIB_CFLAGS) -DSS_TEST_IMPL='"../$(IMPL)"' $< $(LDFLAGS) -o $@

$(BUILD_DIR)/example_%: examples/example_%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< $(LIB_NAME) $(LDFLAGS) -o $@

$(BENCH_BIN): benchmarks/benchmark_ss_lib.c $(LIB_NAME)
	$(CC) $(CFLAGS) -O3 $< $(LIB_NAME) $(LDFLAGS) -lm -o $@

# ---------------------------------------------------------------- tests

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; $$t > $$t.log 2>&1 || { cat $$t.log; exit 1; }; tail -1 $$t.log; done

EMBEDDED_FEATURES = -DSS_EMBEDDED_BUILD -DSS_MAX_SIGNALS=16 -DSS_MAX_SLOTS=32 \
    -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1 -DSS_ENABLE_ISR_SAFE=1

# Embedded examples against a library built with the embedded preset
test_embedded:
	@$(MAKE) --no-print-directory build/embedded/example_embedded_simple \
	    build/embedded/example_embedded BUILD_DIR=build/embedded FEATURES="$(EMBEDDED_FEATURES)"
	@build/embedded/example_embedded_simple && build/embedded/example_embedded

test-static:
	@$(MAKE) --no-print-directory test BUILD_DIR=build/static \
	    FEATURES="$(FEATURES) -DSS_USE_STATIC_MEMORY=1"

test-minimal:
	@$(MAKE) --no-print-directory test BUILD_DIR=build/minimal \
	    FEATURES="-DSS_MINIMAL_BUILD -DSS_ENABLE_DEBUG_TRACE=1"

# The C89 copy, compiled as strict C89, against the same test suite
test-c89:
	@$(MAKE) --no-print-directory test IMPL=src/ss_lib_c89.c LIB_STD="-std=c89 -Werror" \
	    BUILD_DIR=build/c89
	@$(MAKE) --no-print-directory test IMPL=src/ss_lib_c89.c LIB_STD="-std=c89 -Werror" \
	    BUILD_DIR=build/c89-static FEATURES="$(FEATURES) -DSS_USE_STATIC_MEMORY=1"

# The suite built as the newest C standard $(CC) supports
test-newest:
	@std=$$(scripts/check_standards.sh --newest $(CC)) && echo "== -std=$$std" && \
	    $(MAKE) --no-print-directory test BUILD_DIR=build/$$std CSTD=-std=$$std \
	        LIB_STD="-std=$$std -Werror"

# Library, public header and single header compile warning-free from C89 to
# the newest standard $(CC) supports
check-standards:
	@scripts/check_standards.sh $(CC)

test-all: test test-static test-minimal test-c89 test-newest check-standards \
          check-single-header check-version check-install

test-asan:
	@$(MAKE) --no-print-directory test SANITIZER=address BUILD_DIR=build/asan
	@$(MAKE) --no-print-directory test SANITIZER=address BUILD_DIR=build/asan-static \
	    FEATURES="$(FEATURES) -DSS_USE_STATIC_MEMORY=1"

test-tsan:
	@$(MAKE) --no-print-directory test SANITIZER=thread BUILD_DIR=build/tsan
	@$(MAKE) --no-print-directory test SANITIZER=thread BUILD_DIR=build/tsan-trace \
	    FEATURES="$(FEATURES) -DSS_ENABLE_DEBUG_TRACE=1"

test-ubsan:
	@$(MAKE) --no-print-directory test SANITIZER=undefined BUILD_DIR=build/ubsan

test-msan:
	@$(MAKE) --no-print-directory test SANITIZER=memory BUILD_DIR=build/msan

# test_threads is left to ThreadSanitizer (valgrind serializes threads)
test-valgrind: $(TEST_BINS)
	@for t in $(filter-out %test_threads,$(TEST_BINS)); do \
	    valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all \
	        --error-exitcode=1 -q $$t > /dev/null || exit 1; done
	@echo "Valgrind: no errors, no leaks"

coverage:
	@$(MAKE) --no-print-directory test COVERAGE=1 BUILD_DIR=build/coverage
	lcov --capture --directory . --output-file coverage.info
	lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
	genhtml coverage.info --output-directory coverage_report

# ---------------------------------------------------------------- tools

benchmark: $(BENCH_BIN)
	@$(BENCH_BIN)

benchmark-all:
	@benchmarks/run_benchmarks.sh

single-header:
	@./create_single_header.sh include/ss_lib_single.h

# Fails when the committed single header is stale or does not compile
check-single-header: | $(BUILD_DIR)
	@./create_single_header.sh $(BUILD_DIR)/ss_lib_single.h > /dev/null
	@diff -q include/ss_lib_single.h $(BUILD_DIR)/ss_lib_single.h > /dev/null || \
	    { echo "include/ss_lib_single.h is stale: run 'make single-header'"; exit 1; }
	@printf '#define SS_IMPLEMENTATION\n#include "ss_lib_single.h"\nint main(void){return ss_init()==SS_OK?0:1;}\n' \
	    > $(BUILD_DIR)/single_header_check.c
	@$(CC) $(CSTD) $(WARN) -Werror -Iinclude $(BUILD_DIR)/single_header_check.c $(LDFLAGS) \
	    -o $(BUILD_DIR)/single_header_check && $(BUILD_DIR)/single_header_check
	@echo "Single header is current and compiles"

check-version:
	@scripts/check_version.sh

# Static analysis, as CI runs it. SS_ISR_CRITICAL_ENTER/EXIT are function-like
# macros supplied by the user, so that branch is checked with them defined
# that way (cppcheck would otherwise try them as plain values: "1();").
CPPCHECK_FLAGS = --enable=warning,performance,portability --error-exitcode=1 \
    --inline-suppr --suppress=missingIncludeSystem -q -Iinclude
cppcheck:
	cppcheck $(CPPCHECK_FLAGS) -USS_ISR_CRITICAL_ENTER -USS_ISR_CRITICAL_EXIT \
	    src/ss_lib.c include/
	cppcheck $(CPPCHECK_FLAGS) -DSS_ENABLE_ISR_SAFE=1 \
	    "-DSS_ISR_CRITICAL_ENTER()=" "-DSS_ISR_CRITICAL_EXIT()=" src/ss_lib.c

# The CHANGELOG section that becomes the GitHub release notes
release-notes:
	@scripts/release_notes.sh $(VERSION)

# ---------------------------------------------------------------- install

PREFIX ?= /usr/local
INSTALL_INC_DIR = $(PREFIX)/include/ss_lib
INSTALL_LIB_DIR = $(PREFIX)/lib
INSTALL_PC_DIR = $(PREFIX)/lib/pkgconfig
INSTALL_CMAKE_DIR = $(PREFIX)/lib/cmake/ss_lib
# FEATURES as a CMake list: -DA=1 -DB=1 -> A=1;B=1
empty :=
CMAKE_DEFINITIONS = $(subst $(empty) $(empty),;,$(strip $(patsubst -D%,%,$(filter -D%,$(FEATURES)))))

install: $(LIB_NAME) $(LIB_SO)
	@mkdir -p $(INSTALL_INC_DIR) $(INSTALL_LIB_DIR) $(INSTALL_PC_DIR) $(INSTALL_CMAKE_DIR)
	@install -m 644 include/ss_lib.h include/ss_config.h $(INSTALL_INC_DIR)
	@install -m 644 $(LIB_NAME) $(INSTALL_LIB_DIR)
	@$(INSTALL_SHARED)
	@if [ -n "$(SHARED_SONAME)" ]; then \
	    ln -sf $(SHARED_REAL) $(INSTALL_LIB_DIR)/$(SHARED_SONAME); \
	    ln -sf $(SHARED_SONAME) $(INSTALL_LIB_DIR)/libss_lib.$(SHARED_EXT); fi
	@# Callers must use the library's FEATURES; pkg-config hands them on
	@sed -e 's|@CMAKE_INSTALL_PREFIX@|$(PREFIX)|' -e 's|@PROJECT_VERSION@|$(VERSION)|' \
	     -e 's|@CMAKE_THREAD_LIBS_INIT@|-pthread|' -e 's|@SS_FEATURE_CFLAGS@|$(strip $(FEATURES))|' \
	     ss_lib.pc.in > $(INSTALL_PC_DIR)/ss_lib.pc
	@# find_package(ss_lib), the same package the CMake build installs
	@for f in ss_libConfig ss_libConfigVersion ss_libTargets; do \
	    sed -e 's|@PROJECT_VERSION@|$(VERSION)|' -e 's|@PROJECT_VERSION_MAJOR@|$(MAJOR)|' \
	        -e 's|@SS_ENABLE_THREAD_SAFETY@|ON|' -e 's|@SHARED_REAL@|$(SHARED_REAL)|' \
	        -e 's|@SS_FEATURE_DEFINITIONS@|$(CMAKE_DEFINITIONS)|' \
	        cmake/$$f.cmake.in > $(INSTALL_CMAKE_DIR)/$$f.cmake || exit 1; done
	@echo "Installed SS_Lib $(VERSION) to $(PREFIX)"

# Install into build/, build programs against the installed shared library
# with pkg-config flags and (when cmake is present) find_package, run them,
# then check that uninstall leaves no library, header or package file behind
INSTALL_CHECK = $(abspath $(BUILD_DIR))/install-check
check-install:
	@$(MAKE) --no-print-directory install PREFIX=$(INSTALL_CHECK) > /dev/null
	@scripts/check_install.sh $(INSTALL_CHECK) "$(CC)"
	@$(MAKE) --no-print-directory uninstall PREFIX=$(INSTALL_CHECK)
	@left=$$(find $(INSTALL_CHECK)/lib $(INSTALL_CHECK)/include -type f -o -type l); \
	    [ -z "$$left" ] || { echo "uninstall left behind: $$left"; exit 1; }
	@echo "make install and uninstall work"

uninstall:
	@rm -rf $(INSTALL_INC_DIR)
	@rm -f $(addprefix $(INSTALL_LIB_DIR)/,libss_lib.a libss_lib.$(SHARED_EXT) \
	    $(SHARED_REAL) $(SHARED_SONAME))
	@rm -f $(INSTALL_PC_DIR)/ss_lib.pc
	@rm -rf $(INSTALL_CMAKE_DIR)

docs:
	@mkdir -p docs/api
	@SS_VERSION=$(VERSION) doxygen Doxyfile || echo "Doxygen not found, skipping"

info:
	@echo "SS_Lib $(VERSION): CC=$(CC) IMPL=$(IMPL) FEATURES=$(FEATURES)"

clean:
	rm -rf build coverage.info coverage_report

help:
	@echo "SS_Lib $(VERSION) build targets"
	@echo ""
	@echo "  all                 library, tests and examples"
	@echo "  lib / shared        static / shared library"
	@echo "  test                build and run the test suite"
	@echo "  test-static         ... with SS_USE_STATIC_MEMORY=1"
	@echo "  test-minimal        ... with SS_MINIMAL_BUILD"
	@echo "  test-c89            ... against src/ss_lib_c89.c built as C89"
	@echo "  test-newest         ... built as the newest C standard CC supports"
	@echo "  check-standards     compile as C89 ... newest standard with -Werror"
	@echo "  test-all            all of the above plus consistency checks"
	@echo "  test-asan/-tsan/-ubsan/-msan/-valgrind   sanitizer runs"
	@echo "  coverage            lcov report"
	@echo "  benchmark(-all)     benchmarks"
	@echo "  single-header       regenerate include/ss_lib_single.h"
	@echo "  check-single-header fail if the committed single header is stale"
	@echo "  check-version       fail if version numbers disagree"
	@echo "  release-notes       print this version's CHANGELOG section"
	@echo "  cppcheck            static analysis, as CI runs it"
	@echo "  install/uninstall   PREFIX=$(PREFIX)"
	@echo "  check-install       install into build/, run a program against it, uninstall"
	@echo ""
	@echo "Variables: CC CSTD FEATURES IMPL BUILD_DIR SANITIZER COVERAGE PREFIX"
