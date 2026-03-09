/*
 * test_sanitize_name.c
 * ---------------------
 * Unit tests for sanitize_name() — the client-side function that replaces
 * path separator characters in received filenames before writing them to
 * the client_files/ directory, preventing directory traversal via crafted
 * server-supplied filenames.
 *
 * Test cases:
 *   1. Forward slash '/' is replaced with '_'
 *   2. Backslash '\' is replaced with '_'
 *   3. A clean filename with no separators is left unchanged
 *
 * Run via:  make test
 */

#include "unity.h"
#include <string.h>
#include <stddef.h>

/* Function under test — copied from client.c */
static void sanitize_name(const char *in, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < cap; i++) {
        char c = in[i];
        if (c == '/' || c == '\\') c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: forward slash replaced */
void test_slash_replaced(void) {
    char out[64];
    sanitize_name("dir/file.txt", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("dir_file.txt", out);
}

/* Test 2: backslash replaced */
void test_backslash_replaced(void) {
    char out[64];
    sanitize_name("dir\\file.txt", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("dir_file.txt", out);
}

/* Test 3: clean filename unchanged */
void test_clean_name_unchanged(void) {
    char out[64];
    sanitize_name("hello.txt", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello.txt", out);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_slash_replaced);
    RUN_TEST(test_backslash_replaced);
    RUN_TEST(test_clean_name_unchanged);
    return UNITY_END();
}
