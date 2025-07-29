cmake_minimum_required(VERSION 3.10)
project(ss_lib VERSION 2.0.0 LANGUAGES C)

# Options
option(SS_BUILD_SHARED "Build shared library" OFF)
option(SS_BUILD_TESTS "Build tests" ON)
option(SS_BUILD_EXAMPLES "Build examples" ON)
option(SS_USE_STATIC_MEMORY "Use static memory allocation" OFF)
option(SS_ENABLE_THREAD_SAFETY "Enable thread safety" ON)
option(SS_ENABLE_PERFORMANCE_STATS "Enable performance statistics" OFF)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Configuration
if(SS_USE_STATIC_MEMORY)
    add_compile_definitions(SS_USE_STATIC_MEMORY=1)
    set(SS_MAX_SIGNALS "32" CACHE STRING "Maximum number of signals")
    set(SS_MAX_SLOTS "128" CACHE STRING "Maximum number of slots")
    add_compile_definitions(SS_MAX_SIGNALS=${SS_MAX_SIGNALS})
    add_compile_definitions(SS_MAX_SLOTS=${SS_MAX_SLOTS})
endif()

if(SS_ENABLE_THREAD_SAFETY)
    add_compile_definitions(SS_ENABLE_THREAD_SAFETY=1)
    find_package(Threads REQUIRED)
endif()

if(SS_ENABLE_PERFORMANCE_STATS)
    add_compile_definitions(SS_ENABLE_PERFORMANCE_STATS=1)
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Library sources
set(LIB_SOURCES
    src/ss_lib_v2.c
)

set(LIB_HEADERS
    include/ss_lib_v2.h
    include/ss_config.h
)

# Build library
if(SS_BUILD_SHARED)
    add_library(ss_lib SHARED ${LIB_SOURCES})
else()
    add_library(ss_lib STATIC ${LIB_SOURCES})
endif()

target_include_directories(ss_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(SS_ENABLE_THREAD_SAFETY)
    target_link_libraries(ss_lib Threads::Threads)
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
    
    add_executable(test_ss_lib tests/test_ss_lib.c src/ss_lib.c)
    add_executable(test_v2_simple tests/test_v2_simple.c)
    
    target_link_libraries(test_v2_simple ss_lib)
    
    if(SS_ENABLE_THREAD_SAFETY)
        target_link_libraries(test_ss_lib Threads::Threads)
    endif()
    
    add_test(NAME test_basic COMMAND test_ss_lib)
    add_test(NAME test_v2 COMMAND test_v2_simple)
endif()

# Examples
if(SS_BUILD_EXAMPLES)
    add_executable(example_usage examples/example_usage.c src/ss_lib.c)
    add_executable(example_embedded_simple examples/example_embedded_simple.c)
    
    target_link_libraries(example_embedded_simple ss_lib)
    
    if(SS_ENABLE_THREAD_SAFETY)
        target_link_libraries(example_usage Threads::Threads)
    endif()
endif()

# Package configuration
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ss_libConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
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
message(STATUS "")