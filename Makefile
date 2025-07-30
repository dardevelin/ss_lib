CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O2 -Iinclude
LDFLAGS = -pthread

# Sanitizer flags (use with make SANITIZER=address/thread/undefined)
ifeq ($(SANITIZER),address)
    CFLAGS += -fsanitize=address -fno-omit-frame-pointer -g
    LDFLAGS += -fsanitize=address
else ifeq ($(SANITIZER),thread)
    CFLAGS += -fsanitize=thread -g
    LDFLAGS += -fsanitize=thread
else ifeq ($(SANITIZER),undefined)
    CFLAGS += -fsanitize=undefined -g
    LDFLAGS += -fsanitize=undefined
else ifeq ($(SANITIZER),memory)
    CC = clang  # Memory sanitizer requires clang
    CFLAGS += -fsanitize=memory -fno-omit-frame-pointer -g
    LDFLAGS += -fsanitize=memory
endif

# Coverage flags (use with make COVERAGE=1)
ifeq ($(COVERAGE),1)
    CFLAGS += --coverage -fprofile-arcs -ftest-coverage -O0
    LDFLAGS += --coverage
endif

# Directories
SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
EXAMPLE_DIR = examples
BUILD_DIR = build

# Create build directory
$(shell mkdir -p $(BUILD_DIR))

# V2 Library
LIB_V2_SRC = $(SRC_DIR)/ss_lib_v2.c
LIB_V2_OBJ = $(BUILD_DIR)/ss_lib_v2.o
LIB_V2_NAME = $(BUILD_DIR)/libsslib_v2.a
LIB_V2_SO = $(BUILD_DIR)/libsslib_v2.so

# V1 Library (legacy)
LIB_SRC = $(SRC_DIR)/ss_lib.c
LIB_OBJ = $(BUILD_DIR)/ss_lib.o
LIB_NAME = $(BUILD_DIR)/libsslib.a
LIB_SO = $(BUILD_DIR)/libsslib.so

# Platform-specific shared library extension
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    SHARED_EXT = dylib
    SHARED_FLAGS = -dynamiclib -install_name @rpath/$(notdir $@)
else
    SHARED_EXT = so
    SHARED_FLAGS = -shared
endif

# Test programs
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

# Examples
EXAMPLE_SRCS = $(wildcard $(EXAMPLE_DIR)/*.c)
EXAMPLE_BINS = $(patsubst $(EXAMPLE_DIR)/%.c,$(BUILD_DIR)/%,$(EXAMPLE_SRCS))

# Benchmarks
BENCH_DIR = benchmarks
BENCH_SRC = $(BENCH_DIR)/benchmark_ss_lib.c
BENCH_BIN = $(BUILD_DIR)/benchmark_ss_lib

.PHONY: all clean test lib lib_v2 examples docs install shared benchmark

all: lib_v2 tests examples

lib: $(LIB_NAME)

lib_v2: $(LIB_V2_NAME)

shared: $(LIB_V2_SO) $(LIB_SO)

tests: $(TEST_BINS)

examples: $(EXAMPLE_BINS)

# Library builds
$(LIB_NAME): $(LIB_OBJ)
	ar rcs $@ $^

$(LIB_V2_NAME): $(LIB_V2_OBJ)
	ar rcs $@ $^

# Shared library builds
$(LIB_SO): $(LIB_OBJ)
	$(CC) $(SHARED_FLAGS) -o $@ $^ $(LDFLAGS)

$(LIB_V2_SO): $(LIB_V2_OBJ)
	$(CC) $(SHARED_FLAGS) -o $@ $^ $(LDFLAGS)

# Special build rules for V2 with all features enabled
$(BUILD_DIR)/ss_lib_v2.o: $(SRC_DIR)/ss_lib_v2.c
	$(CC) $(CFLAGS) -fPIC -DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1 -c $< -o $@

# Generic object file rule
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Test programs
$(BUILD_DIR)/test_ss_lib: $(TEST_DIR)/test_ss_lib.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lsslib $(LDFLAGS) -o $@

$(BUILD_DIR)/test_v2_simple: $(TEST_DIR)/test_v2_simple.c $(LIB_V2_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lsslib_v2 $(LDFLAGS) -o $@

# Examples
$(BUILD_DIR)/example_usage: $(EXAMPLE_DIR)/example_usage.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lsslib $(LDFLAGS) -o $@

$(BUILD_DIR)/example_embedded: $(EXAMPLE_DIR)/example_embedded.c $(LIB_V2_NAME)
	$(CC) $(CFLAGS) -DSS_ENABLE_ISR_SAFE=1 $< -L$(BUILD_DIR) -lsslib_v2 $(LDFLAGS) -o $@

$(BUILD_DIR)/example_embedded_simple: $(EXAMPLE_DIR)/example_embedded_simple.c $(LIB_V2_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lsslib_v2 $(LDFLAGS) -o $@

# Run tests
test: tests
	@echo "Running tests..."
	@$(BUILD_DIR)/test_ss_lib
	@$(BUILD_DIR)/test_v2_simple

test_embedded: $(BUILD_DIR)/example_embedded_simple
	@echo "Running embedded example..."
	@$(BUILD_DIR)/example_embedded_simple

# Documentation
docs:
	@echo "Building documentation..."
	@if command -v doxygen > /dev/null; then \
		cd docs && doxygen; \
	else \
		echo "Doxygen not found. Please install doxygen to build documentation."; \
	fi

# Installation
PREFIX ?= /usr/local

install: lib_v2 ss_lib.pc
	@echo "Installing to $(PREFIX)..."
	@mkdir -p $(PREFIX)/lib $(PREFIX)/include/ss_lib $(PREFIX)/lib/pkgconfig
	@cp $(LIB_V2_NAME) $(PREFIX)/lib/
	@cp $(INC_DIR)/*.h $(PREFIX)/include/ss_lib/
	@sed "s|^prefix=.*|prefix=$(PREFIX)|" ss_lib.pc > $(PREFIX)/lib/pkgconfig/ss_lib.pc
	@echo "Installation complete!"
	@echo ""
	@echo "To use SS_Lib in your project:"
	@echo "  pkg-config --cflags ss_lib"
	@echo "  pkg-config --libs ss_lib"

# Clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f ss_lib_single.h

# Sanitizer targets
test-asan:
	@echo "Running tests with AddressSanitizer..."
	@$(MAKE) clean
	@$(MAKE) SANITIZER=address tests
	@ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1 $(BUILD_DIR)/test_ss_lib
	@ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1 $(BUILD_DIR)/test_v2_simple

test-tsan:
	@echo "Running tests with ThreadSanitizer..."
	@$(MAKE) clean
	@$(MAKE) SANITIZER=thread tests
	@TSAN_OPTIONS=halt_on_error=1:history_size=7 $(BUILD_DIR)/test_ss_lib
	@TSAN_OPTIONS=halt_on_error=1:history_size=7 $(BUILD_DIR)/test_v2_simple

test-ubsan:
	@echo "Running tests with UndefinedBehaviorSanitizer..."
	@$(MAKE) clean
	@$(MAKE) SANITIZER=undefined tests
	@UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 $(BUILD_DIR)/test_ss_lib
	@UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 $(BUILD_DIR)/test_v2_simple

test-msan:
	@echo "Running tests with MemorySanitizer..."
	@$(MAKE) clean
	@$(MAKE) SANITIZER=memory CC=clang tests
	@MSAN_OPTIONS=halt_on_error=1 $(BUILD_DIR)/test_ss_lib
	@MSAN_OPTIONS=halt_on_error=1 $(BUILD_DIR)/test_v2_simple

# Valgrind target
test-valgrind: tests
	@echo "Running tests with Valgrind..."
	@command -v valgrind >/dev/null 2>&1 || { echo "Valgrind not installed. Please install valgrind."; exit 1; }
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	          --suppressions=valgrind.supp --error-exitcode=1 \
	          $(BUILD_DIR)/test_ss_lib
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	          --suppressions=valgrind.supp --error-exitcode=1 \
	          $(BUILD_DIR)/test_v2_simple

# Memory check with detailed output
memcheck: tests
	@echo "Running detailed memory check..."
	@command -v valgrind >/dev/null 2>&1 || { echo "Valgrind not installed. Please install valgrind."; exit 1; }
	@valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	          --suppressions=valgrind.supp --gen-suppressions=all \
	          --verbose --log-file=valgrind.log \
	          $(BUILD_DIR)/test_ss_lib
	@echo "Valgrind log saved to valgrind.log"

# Coverage target
coverage: clean
	@echo "Building with coverage enabled..."
	@$(MAKE) COVERAGE=1 tests
	@echo "Running tests for coverage..."
	@$(BUILD_DIR)/test_ss_lib
	@$(BUILD_DIR)/test_v2_simple
	@echo "Generating coverage report..."
	@lcov --capture --directory $(BUILD_DIR) --output-file coverage.info
	@lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
	@lcov --list coverage.info
	@echo "Coverage report generated. For HTML report, run: genhtml coverage.info --output-directory coverage-report"

# Benchmark targets
benchmark: $(BENCH_BIN)
	@echo "Running benchmarks..."
	@$(BENCH_BIN)

$(BENCH_BIN): $(BENCH_SRC) $(LIB_V2_NAME)
	@mkdir -p $(dir $@)
	$(CC) -O3 -march=native $(CFLAGS) $< -L$(BUILD_DIR) -lsslib_v2 $(LDFLAGS) -o $@

benchmark-all:
	@echo "Running comprehensive benchmarks..."
	@$(BENCH_DIR)/run_benchmarks.sh

# Help
help:
	@echo "SS_Lib Makefile targets:"
	@echo "  all         - Build everything (default)"
	@echo "  lib_v2      - Build the V2 static library"
	@echo "  shared      - Build shared libraries (.so/.dylib)"
	@echo "  tests       - Build test programs"
	@echo "  examples    - Build example programs"
	@echo "  test        - Run tests"
	@echo "  test-asan   - Run tests with AddressSanitizer"
	@echo "  test-tsan   - Run tests with ThreadSanitizer"
	@echo "  test-ubsan  - Run tests with UndefinedBehaviorSanitizer"
	@echo "  test-msan   - Run tests with MemorySanitizer (clang only)"
	@echo "  test-valgrind - Run tests with Valgrind memory checker"
	@echo "  coverage    - Generate code coverage report"
	@echo "  benchmark   - Run basic performance benchmark"
	@echo "  benchmark-all - Run comprehensive benchmark suite"
	@echo "  docs        - Build documentation (requires Doxygen)"
	@echo "  install     - Install library and headers"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Build options:"
	@echo "  make SANITIZER=address  - Build with AddressSanitizer"
	@echo "  make SANITIZER=thread   - Build with ThreadSanitizer"
	@echo "  make SANITIZER=undefined - Build with UndefinedBehaviorSanitizer"
	@echo "  make COVERAGE=1         - Build with code coverage"
	@echo "  make CC=clang           - Build with clang instead of gcc"