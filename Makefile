CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -O2 -Iinclude
LDFLAGS = -pthread

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

.PHONY: all clean test lib lib_v2 examples docs install shared

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

install: lib_v2
	@echo "Installing to $(PREFIX)..."
	@mkdir -p $(PREFIX)/lib $(PREFIX)/include/ss_lib
	@cp $(LIB_V2_NAME) $(PREFIX)/lib/
	@cp $(INC_DIR)/*.h $(PREFIX)/include/ss_lib/
	@echo "Installation complete!"

# Clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f ss_lib_single.h

# Help
help:
	@echo "SS_Lib Makefile targets:"
	@echo "  all         - Build everything (default)"
	@echo "  lib_v2      - Build the V2 static library"
	@echo "  shared      - Build shared libraries (.so/.dylib)"
	@echo "  tests       - Build test programs"
	@echo "  examples    - Build example programs"
	@echo "  test        - Run tests"
	@echo "  docs        - Build documentation (requires Doxygen)"
	@echo "  install     - Install library and headers"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this help message"