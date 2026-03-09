/*
 * test_cancel_token.c
 * --------------------
 * Unit tests for is_cancel() — the client-side function that checks whether
 * a user's input matches the special escape token "!cancel", which aborts
 * the current operation and returns to the main menu without sending any
 * server request.
 *
 * Test cases:
 *   1. Exact token "!cancel" is detected as cancel
 *   2. A normal filename is not treated as cancel
 *   3. An empty string is not treated as cancel
 *   4. Partial matches (too short or too long) are not cancel
 *
 * Run via:  make test
 */

#include "unity.h"
#include <string.h>

#define CANCEL_TOKEN "!cancel"

/* Function under test — copied from client.c */
static int is_cancel(const char *s) {
    return (strcmp(s, CANCEL_TOKEN) == 0);
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: exact cancel token detected */
void test_cancel_token_detected(void) {
    TEST_ASSERT_EQUAL_INT(1, is_cancel("!cancel"));
}

/* Test 2: normal filename is not cancel */
void test_normal_filename_not_cancel(void) {
    TEST_ASSERT_EQUAL_INT(0, is_cancel("a.txt"));
}

/* Test 3: empty string is not cancel */
void test_empty_string_not_cancel(void) {
    TEST_ASSERT_EQUAL_INT(0, is_cancel(""));
}

/* Test 4: partial or extended token is not cancel */
void test_partial_token_not_cancel(void) {
    TEST_ASSERT_EQUAL_INT(0, is_cancel("!cance"));
    TEST_ASSERT_EQUAL_INT(0, is_cancel("!cancell"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cancel_token_detected);
    RUN_TEST(test_normal_filename_not_cancel);
    RUN_TEST(test_empty_string_not_cancel);
    RUN_TEST(test_partial_token_not_cancel);
    return UNITY_END();
}
