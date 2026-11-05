cmake_minimum_required(VERSION 3.10)

# The version lives in include/ss_lib.h (SS_VERSION_STRING) only
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/ss_lib.h" _ss_version_line
     REGEX "^#define SS_VERSION_STRING ")
string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" SS_VERSION "${_ss_version_line}")
project(ss_lib VERSION ${SS_VERSION} LANGUAGES C)

# Options
option(SS_BUILD_SHARED "Build shared library" OFF)
option(SS_BUILD_TESTS "Build tests" ON)
option(SS_BUILD_EXAMPLES "Build examples" ON)
option(SS_USE_STATIC_MEMORY "Use static memory allocation" OFF)
option(SS_ENABLE_THREAD_SAFETY "Enable thread safety" ON)
option(SS_ENABLE_PERFORMANCE_STATS "Enable performance statistics" OFF)
option(SS_ENABLE_ISR_SAFE "Enable ISR-safe emission" OFF)
option(SS_ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(SS_ENABLE_COVERAGE "Enable code coverage" OFF)

# C standard: the sources are C89-compatible; 11 stays the default and
# -DCMAKE_C_STANDARD=90|99|17|23 selects another
if(NOT DEFINED CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 11)
endif()
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
    
    # Sanitizer support
    if(SS_ENABLE_SANITIZERS)
        set(SANITIZER_TYPE "address" CACHE STRING "Type of sanitizer (address, thread, undefined, memory)")
        if(SANITIZER_TYPE STREQUAL "address")
            add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
            add_link_options(-fsanitize=address)
        elseif(SANITIZER_TYPE STREQUAL "thread")
            add_compile_options(-fsanitize=thread)
            add_link_options(-fsanitize=thread)
        elseif(SANITIZER_TYPE STREQUAL "undefined")
            add_compile_options(-fsanitize=undefined)
            add_link_options(-fsanitize=undefined)
        elseif(SANITIZER_TYPE STREQUAL "memory")
            if(CMAKE_C_COMPILER_ID MATCHES "Clang")
                add_compile_options(-fsanitize=memory -fno-omit-frame-pointer)
                add_link_options(-fsanitize=memory)
            else()
                message(WARNING "Memory sanitizer requires Clang")
            endif()
        endif()
    endif()
    
    # Coverage support
    if(SS_ENABLE_COVERAGE)
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage -O0)
        add_link_options(--coverage)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
    if(SS_ENABLE_SANITIZERS)
        add_compile_options(/fsanitize=address)
    endif()
endif()

# Configuration
if(SS_USE_STATIC_MEMORY)
    list(APPEND SS_FEATURE_DEFINITIONS SS_USE_STATIC_MEMORY=1)
    set(SS_MAX_SIGNALS "32" CACHE STRING "Maximum number of signals")
    set(SS_MAX_SLOTS "128" CACHE STRING "Maximum number of slots")
    list(APPEND SS_FEATURE_DEFINITIONS SS_MAX_SIGNALS=${SS_MAX_SIGNALS})
    list(APPEND SS_FEATURE_DEFINITIONS SS_MAX_SLOTS=${SS_MAX_SLOTS})
endif()

# Always pass 0 or 1: an absent definition means "header default", which
# for thread safety is 1.
foreach(opt SS_ENABLE_THREAD_SAFETY SS_ENABLE_PERFORMANCE_STATS SS_ENABLE_ISR_SAFE)
    if(${opt})
        list(APPEND SS_FEATURE_DEFINITIONS ${opt}=1)
    else()
        list(APPEND SS_FEATURE_DEFINITIONS ${opt}=0)
    endif()
endforeach()
if(SS_ENABLE_THREAD_SAFETY)
    find_package(Threads REQUIRED)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Library sources
set(LIB_SOURCES
    src/ss_lib.c
)

set(LIB_HEADERS
    include/ss_lib.h
    include/ss_config.h
)

# Build library
if(SS_BUILD_SHARED)
    add_library(ss_lib SHARED ${LIB_SOURCES})
    # semver: the major version is the ABI (libss_lib.so.MAJOR)
    set_target_properties(ss_lib PROPERTIES
        VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
    # macOS: the installed dylib names its absolute path, so programs linked
    # against it start without an rpath (as with make install)
    if(CMAKE_VERSION VERSION_LESS 3.17)
        set_target_properties(ss_lib PROPERTIES INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib")
    else()
        set_target_properties(ss_lib PROPERTIES INSTALL_NAME_DIR "$<INSTALL_PREFIX>/lib")
    endif()
else()
    add_library(ss_lib STATIC ${LIB_SOURCES})
endif()

target_include_directories(ss_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    # <ss_lib.h>, as with pkg-config; <ss_lib/ss_lib.h> keeps working
    $<INSTALL_INTERFACE:include/ss_lib>
    $<INSTALL_INTERFACE:include>
)

# Feature definitions are part of the ABI: every consumer must see them
target_compile_definitions(ss_lib PUBLIC ${SS_FEATURE_DEFINITIONS})

if(SS_ENABLE_THREAD_SAFETY)
    target_link_libraries(ss_lib PUBLIC Threads::Threads)
endif()

# Installation
install(TARGETS ss_lib
    EXPORT ss_libTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${LIB_HEADERS}
    DESTINATION include/ss_lib
)

# Tests
if(SS_BUILD_TESTS)
    enable_testing()

    add_executable(test_ss_lib tests/test_ss_lib.c)
    target_link_libraries(test_ss_lib ss_lib)
    add_executable(test_simple tests/test_simple.c)
    target_link_libraries(test_simple ss_lib)
    # Compiles the implementation itself, with a tracking allocator
    add_executable(test_memory tests/test_memory.c)
    target_compile_definitions(test_memory PRIVATE ${SS_FEATURE_DEFINITIONS})
    if(SS_ENABLE_THREAD_SAFETY)
        target_link_libraries(test_memory Threads::Threads)
    endif()
    add_test(NAME test_basic COMMAND test_ss_lib)
    add_test(NAME test_simple COMMAND test_simple)
    add_test(NAME test_memory COMMAND test_memory)
    # Also compiles the implementation, with a hookable ISR critical section
    add_executable(test_isr tests/test_isr.c)
    target_compile_definitions(test_isr PRIVATE ${SS_FEATURE_DEFINITIONS})
    if(SS_ENABLE_THREAD_SAFETY)
        target_link_libraries(test_isr Threads::Threads)
    endif()
    add_test(NAME test_isr COMMAND test_isr)
    add_executable(test_reentrancy tests/test_reentrancy.c)
    target_link_libraries(test_reentrancy ss_lib)
    add_test(NAME test_reentrancy COMMAND test_reentrancy)

    if(SS_ENABLE_THREAD_SAFETY AND NOT WIN32)
        add_executable(test_threads tests/test_threads.c)
        target_link_libraries(test_threads ss_lib Threads::Threads)
        add_test(NAME test_threads COMMAND test_threads)
    endif()
endif()

# Examples
if(SS_BUILD_EXAMPLES)
    add_executable(example_usage examples/example_usage.c)
    target_link_libraries(example_usage ss_lib)
    add_executable(example_embedded_simple examples/example_embedded_simple.c)
    target_link_libraries(example_embedded_simple ss_lib)
endif()

# Package configuration
include(CMakePackageConfigHelpers)

# Generate pkg-config file; it carries the feature definitions too
set(SS_FEATURE_CFLAGS "")
foreach(def ${SS_FEATURE_DEFINITIONS})
    string(APPEND SS_FEATURE_CFLAGS " -D${def}")
endforeach()
string(STRIP "${SS_FEATURE_CFLAGS}" SS_FEATURE_CFLAGS)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ss_lib.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ss_lib.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ss_lib.pc"
    DESTINATION lib/pkgconfig
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ss_libConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT ss_libTargets
    FILE ss_libTargets.cmake
    NAMESPACE ss_lib::
    DESTINATION lib/cmake/ss_lib
)

configure_file(cmake/ss_libConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/ss_libConfig.cmake"
    @ONLY
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ss_libConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ss_libConfigVersion.cmake"
    DESTINATION lib/cmake/ss_lib
)

# Summary
message(STATUS "")
message(STATUS "SS_Lib Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Shared library: ${SS_BUILD_SHARED}")
message(STATUS "  Static memory: ${SS_USE_STATIC_MEMORY}")
if(SS_USE_STATIC_MEMORY)
    message(STATUS "    Max signals: ${SS_MAX_SIGNALS}")
    message(STATUS "    Max slots: ${SS_MAX_SLOTS}")
endif()
message(STATUS "  Thread safety: ${SS_ENABLE_THREAD_SAFETY}")
message(STATUS "  Performance stats: ${SS_ENABLE_PERFORMANCE_STATS}")
message(STATUS "  Build tests: ${SS_BUILD_TESTS}")
message(STATUS "  Build examples: ${SS_BUILD_EXAMPLES}")
if(SS_ENABLE_SANITIZERS)
    message(STATUS "  Sanitizers: Enabled (${SANITIZER_TYPE})")
endif()
if(SS_ENABLE_COVERAGE)
    message(STATUS "  Code coverage: Enabled")
endif()
message(STATUS "")