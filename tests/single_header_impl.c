/*
 * The library compiled from the generated single header. make test-single
 * builds the test suite with this file as the implementation, so the tests
 * run against include/ss_lib_single.h, the file users copy into their
 * projects. Before 2.2.0 an outdated copy of it kept a use-after-free that
 * the main source no longer had (GHSA-f5pm-2465-f895).
 */
#define SS_IMPLEMENTATION
#include "ss_lib_single.h"
