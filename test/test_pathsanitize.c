/*
 * test_pathsanitize.c
 * -------------------
 * Unit tests for build_fullpath() — the path-sanitisation function used by
 * both server1.c and server2.c to block directory traversal and absolute
 * path injection before any file operation is performed.
 *
 * Test cases:
 *   1. Directory traversal via '..' is rejected
 *   2. Absolute path starting with '/' is rejected
 *   3. A normal filename produces the correct joined path
 *   4. An empty filename is rejected
 *
 * Run via:  make test
 */

#include "unity.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* Function under test — copied from server1.c / server2.c */
static int build_fullpath(char *dst, size_t cap, const char *base_dir, const char *path) {
    if (path[0] == '/' || strstr(path, "..") != NULL) return -1;
    if (path[0] == '\0') return -1;
    int n = snprintf(dst, cap, "%s/%s", base_dir, path);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: directory traversal must be blocked */
void test_traversal_blocked(void) {
    char out[512];
    int r = build_fullpath(out, sizeof(out), "./server1_files", "../etc/passwd");
    TEST_ASSERT_EQUAL_INT(-1, r);
}

/* Test 2: absolute path must be blocked */
void test_absolute_path_blocked(void) {
    char out[512];
    int r = build_fullpath(out, sizeof(out), "./server1_files", "/etc/passwd");
    TEST_ASSERT_EQUAL_INT(-1, r);
}

/* Test 3: valid filename produces correct joined path */
void test_normal_filename_passes(void) {
    char out[512];
    int r = build_fullpath(out, sizeof(out), "./server1_files", "hello.txt");
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_STRING("./server1_files/hello.txt", out);
}

/* Test 4: empty filename must be blocked */
void test_empty_filename_blocked(void) {
    char out[512];
    int r = build_fullpath(out, sizeof(out), "./server1_files", "");
    TEST_ASSERT_EQUAL_INT(-1, r);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_traversal_blocked);
    RUN_TEST(test_absolute_path_blocked);
    RUN_TEST(test_normal_filename_passes);
    RUN_TEST(test_empty_filename_blocked);
    return UNITY_END();
}
