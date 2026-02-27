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

# Library
LIB_SRC = $(SRC_DIR)/ss_lib.c
LIB_OBJ = $(BUILD_DIR)/ss_lib.o
LIB_NAME = $(BUILD_DIR)/libss_lib.a
LIB_SO = $(BUILD_DIR)/libss_lib.so

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

.PHONY: all clean test lib examples docs install shared benchmark

all: lib tests examples

lib: $(LIB_NAME)

shared: $(LIB_SO)

tests: $(TEST_BINS)

examples: $(EXAMPLE_BINS)

# Library builds
$(LIB_NAME): $(LIB_OBJ)
	ar rcs $@ $^

# Shared library builds
$(LIB_SO): $(LIB_OBJ)
	$(CC) $(SHARED_FLAGS) -o $@ $^ $(LDFLAGS)

# Build library with all features enabled
$(BUILD_DIR)/ss_lib.o: $(SRC_DIR)/ss_lib.c
	$(CC) $(CFLAGS) -fPIC -DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1 -c $< -o $@

# Test programs
$(BUILD_DIR)/test_ss_lib: $(TEST_DIR)/test_ss_lib.c $(LIB_NAME)
	$(CC) $(CFLAGS) -DSS_ENABLE_ISR_SAFE=1 -DSS_ENABLE_MEMORY_STATS=1 -DSS_ENABLE_PERFORMANCE_STATS=1 $< -L$(BUILD_DIR) -lss_lib $(LDFLAGS) -o $@

$(BUILD_DIR)/test_simple: $(TEST_DIR)/test_simple.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lss_lib $(LDFLAGS) -o $@

# Examples
$(BUILD_DIR)/example_usage: $(EXAMPLE_DIR)/example_usage.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lss_lib $(LDFLAGS) -o $@

$(BUILD_DIR)/example_embedded: $(EXAMPLE_DIR)/example_embedded.c $(LIB_NAME)
	$(CC) $(CFLAGS) -DSS_ENABLE_ISR_SAFE=1 $< -L$(BUILD_DIR) -lss_lib $(LDFLAGS) -o $@

$(BUILD_DIR)/example_embedded_simple: $(EXAMPLE_DIR)/example_embedded_simple.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -L$(BUILD_DIR) -lss_lib $(LDFLAGS) -o $@

# Run tests
test: tests
	@echo "Running tests..."
	@$(BUILD_DIR)/test_ss_lib
	@$(BUILD_DIR)/test_simple

test_embedded: $(BUILD_DIR)/example_embedded_simple
	@echo "Running embedded example..."
	@$(BUILD_DIR)/example_embedded_simple

# Benchmarks
benchmark: $(BENCH_BIN)
	@echo "Running benchmarks..."
	@$(BENCH_BIN)

$(BENCH_BIN): $(BENCH_SRC) $(LIB_NAME)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 $< -L$(BUILD_DIR) -lss_lib $(LDFLAGS) -lm -o $@

# Run comprehensive benchmarks
benchmark-all: $(BENCH_BIN)
	@chmod +x benchmarks/run_benchmarks.sh
	@benchmarks/run_benchmarks.sh

# Clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f ss_lib_single.h

# Create single header
single-header: $(SRC_DIR)/ss_lib.c $(INC_DIR)/ss_lib.h $(INC_DIR)/ss_config.h
	@./create_single_header.sh

# Installation
PREFIX ?= /usr/local
INSTALL_INC_DIR = $(PREFIX)/include/ss_lib
INSTALL_LIB_DIR = $(PREFIX)/lib
INSTALL_PC_DIR = $(PREFIX)/lib/pkgconfig

install: $(LIB_NAME) $(LIB_SO)
	@echo "Installing SS_Lib to $(PREFIX)..."
	@mkdir -p $(INSTALL_INC_DIR)
	@mkdir -p $(INSTALL_LIB_DIR)
	@mkdir -p $(INSTALL_PC_DIR)
	@install -m 644 $(INC_DIR)/ss_lib.h $(INSTALL_INC_DIR)
	@install -m 644 $(INC_DIR)/ss_config.h $(INSTALL_INC_DIR)
	@install -m 644 $(LIB_NAME) $(INSTALL_LIB_DIR)
	@install -m 755 $(LIB_SO) $(INSTALL_LIB_DIR)
	@echo "Creating pkg-config file..."
	@echo "prefix=$(PREFIX)" > $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "exec_prefix=\$${prefix}" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "libdir=\$${exec_prefix}/lib" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "includedir=\$${prefix}/include/ss_lib" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Name: SS_Lib" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Description: Lightweight signal-slot library for C" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Version: 2.0.0" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Libs: -L\$${libdir} -lss_lib -pthread" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Cflags: -I\$${includedir}" >> $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Installation complete!"

uninstall:
	@echo "Uninstalling SS_Lib from $(PREFIX)..."
	@rm -rf $(INSTALL_INC_DIR)
	@rm -f $(INSTALL_LIB_DIR)/$(notdir $(LIB_NAME))
	@rm -f $(INSTALL_LIB_DIR)/$(notdir $(LIB_SO))
	@rm -f $(INSTALL_PC_DIR)/ss_lib.pc
	@echo "Uninstallation complete!"

# Documentation
docs:
	@echo "Generating documentation..."
	@mkdir -p docs/api
	@doxygen Doxyfile || echo "Doxygen not found, skipping documentation generation"

# Sanitizer convenience targets
test-asan:
	@$(MAKE) clean
	@$(MAKE) SANITIZER=address test

test-tsan:
	@$(MAKE) clean
	@$(MAKE) SANITIZER=thread test

test-ubsan:
	@$(MAKE) clean
	@$(MAKE) SANITIZER=undefined test

test-msan:
	@$(MAKE) clean
	@$(MAKE) CC=clang SANITIZER=memory test

# Valgrind test
test-valgrind: tests
	@echo "Running tests with Valgrind..."
	@valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_ss_lib
	@valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_simple

# Coverage
coverage: clean
	@$(MAKE) COVERAGE=1 test
	@lcov --capture --directory . --output-file coverage.info
	@lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
	@genhtml coverage.info --output-directory coverage_report
	@echo "Coverage report generated in coverage_report/index.html"

# Print configuration
info:
	@echo "SS_Lib Build Configuration:"
	@echo "  CC: $(CC)"
	@echo "  CFLAGS: $(CFLAGS)"
	@echo "  LDFLAGS: $(LDFLAGS)"
	@echo "  PREFIX: $(PREFIX)"
	@echo "  Platform: $(UNAME_S)"

.SUFFIXES:

# Help target
help:
	@echo "SS_Lib Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all             - Build library, tests, and examples"
	@echo "  lib             - Build static library"
	@echo "  shared          - Build shared library"
	@echo "  tests           - Build test programs"
	@echo "  examples        - Build example programs"
	@echo "  test            - Run tests"
	@echo "  benchmark       - Run benchmarks"
	@echo "  benchmark-all   - Run comprehensive benchmarks"
	@echo "  single-header   - Generate single header version"
	@echo "  install         - Install library and headers"
	@echo "  uninstall       - Remove installed files"
	@echo "  clean           - Remove build artifacts"
	@echo "  docs            - Generate documentation"
	@echo ""
	@echo "Test targets:"
	@echo "  test-asan       - Run tests with AddressSanitizer"
	@echo "  test-tsan       - Run tests with ThreadSanitizer"
	@echo "  test-ubsan      - Run tests with UndefinedBehaviorSanitizer"
	@echo "  test-msan       - Run tests with MemorySanitizer (clang only)"
	@echo "  test-valgrind   - Run tests with Valgrind"
	@echo "  coverage        - Generate code coverage report"
	@echo ""
	@echo "Variables:"
	@echo "  CC              - Compiler (default: gcc)"
	@echo "  CFLAGS          - Compiler flags"
	@echo "  LDFLAGS         - Linker flags"
	@echo "  SANITIZER       - Enable sanitizer (address/thread/undefined/memory)"
	@echo "  COVERAGE        - Enable coverage analysis (1/0)"
	@echo "  PREFIX          - Installation prefix (default: /usr/local)"