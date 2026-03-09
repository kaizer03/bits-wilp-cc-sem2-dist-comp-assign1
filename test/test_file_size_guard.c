/*
 * test_file_size_guard.c
 * -----------------------
 * Unit tests for the file-size overflow guard used in read_file_to_buf()
 * in both server1.c and server2.c:
 *
 *   if (st.st_size < 0 || st.st_size > (off_t)UINT32_MAX) return -1;
 *
 * Before reading a file into a uint32_t-sized buffer, the server checks
 * that the on-disk size can be safely cast from off_t (signed 64-bit) to
 * uint32_t (32-bit unsigned). This prevents integer truncation when
 * sending the length-prefixed file payload over the wire.
 *
 * Test cases:
 *   1. Negative size (filesystem error) is rejected
 *   2. Zero size (empty file) is accepted
 *   3. Small normal size is accepted
 *   4. Exactly UINT32_MAX is accepted (largest representable value)
 *   5. UINT32_MAX + 1 is rejected (overflows uint32_t)
 *
 * Run via:  make test
 */

#include "unity.h"
#include <stdint.h>
#include <sys/types.h>  /* off_t */

/* Guard under test — copied verbatim from server1.c / server2.c read_file_to_buf() */
static int file_size_valid(off_t size) {
    if (size < 0 || size > (off_t)UINT32_MAX) return 0; /* invalid */
    return 1; /* valid */
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: negative size — rejected (filesystem/stat error) */
void test_negative_size_rejected(void) {
    TEST_ASSERT_EQUAL_INT(0, file_size_valid(-1));
}

/* Test 2: zero size — accepted (empty file is valid) */
void test_zero_size_accepted(void) {
    TEST_ASSERT_EQUAL_INT(1, file_size_valid(0));
}

/* Test 3: ordinary file size — accepted */
void test_normal_size_accepted(void) {
    TEST_ASSERT_EQUAL_INT(1, file_size_valid(1024));
}

/* Test 4: exactly UINT32_MAX — accepted (largest safe value) */
void test_uint32_max_accepted(void) {
    TEST_ASSERT_EQUAL_INT(1, file_size_valid((off_t)UINT32_MAX));
}

/* Test 5: UINT32_MAX + 1 — rejected (would truncate on cast to uint32_t) */
void test_over_uint32_max_rejected(void) {
    TEST_ASSERT_EQUAL_INT(0, file_size_valid((off_t)UINT32_MAX + 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_negative_size_rejected);
    RUN_TEST(test_zero_size_accepted);
    RUN_TEST(test_normal_size_accepted);
    RUN_TEST(test_uint32_max_accepted);
    RUN_TEST(test_over_uint32_max_rejected);
    return UNITY_END();
}
