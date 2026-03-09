/*
 * test_buffers_equal.c
 * ---------------------
 * Unit tests for buffers_equal() — the server1.c function that performs a
 * byte-perfect comparison of two file content buffers to determine whether
 * the copies held by Server 1 and Server 2 are identical or divergent.
 * This is the core decision function of the original problem statement.
 *
 * Test cases:
 *   1. Two buffers with identical content are equal
 *   2. Two buffers with the same length but different content are not equal
 *   3. Two buffers with different lengths are not equal
 *   4. Two zero-length (empty) buffers are considered equal
 *
 * Run via:  make test
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>

/* Function under test — copied from server1.c */
static int buffers_equal(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen) {
    if (alen != blen) return 0;
    if (alen == 0) return 1;
    return (memcmp(a, b, alen) == 0) ? 1 : 0;
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: identical content — must be equal */
void test_identical_buffers(void) {
    uint8_t a[] = "hello";
    uint8_t b[] = "hello";
    TEST_ASSERT_EQUAL_INT(1, buffers_equal(a, 5, b, 5));
}

/* Test 2: same length, different bytes — must not be equal */
void test_different_content_same_length(void) {
    uint8_t a[] = "hello";
    uint8_t b[] = "world";
    TEST_ASSERT_EQUAL_INT(0, buffers_equal(a, 5, b, 5));
}

/* Test 3: different lengths — must not be equal */
void test_different_lengths(void) {
    uint8_t a[] = "hello";
    uint8_t b[] = "hello!";
    TEST_ASSERT_EQUAL_INT(0, buffers_equal(a, 5, b, 6));
}

/* Test 4: both empty — must be equal */
void test_empty_buffers_equal(void) {
    TEST_ASSERT_EQUAL_INT(1, buffers_equal(NULL, 0, NULL, 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_identical_buffers);
    RUN_TEST(test_different_content_same_length);
    RUN_TEST(test_different_lengths);
    RUN_TEST(test_empty_buffers_equal);
    return UNITY_END();
}
