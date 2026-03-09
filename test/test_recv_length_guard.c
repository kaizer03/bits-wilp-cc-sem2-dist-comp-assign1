/*
 * test_recv_length_guard.c
 * -------------------------
 * Unit tests for the wire-protocol length validation guard used in
 * recv_string() in both server1.c and server2.c:
 *
 *   if (len == 0 || len >= cap || len > MAX_PATH_LEN) return -1;
 *
 * This guard prevents the server from allocating or reading beyond safe
 * bounds when processing client-supplied length prefixes over the network.
 * It is the first line of defence against malformed or malicious frames.
 *
 * Test cases:
 *   1. Zero length is rejected
 *   2. Length within all bounds is accepted
 *   3. Length equal to MAX_PATH_LEN is accepted (exact boundary)
 *   4. Length exceeding MAX_PATH_LEN is rejected
 *   5. Length equal to cap is rejected (>= cap, would overflow the buffer)
 *   6. Length equal to cap-1 is accepted (largest safe value)
 *
 * Run via:  make test
 */

#include "unity.h"
#include <stdint.h>
#include <stddef.h>

#define MAX_PATH_LEN 4096

/* Guard under test — copied verbatim from server1.c / server2.c recv_string() */
static int recv_length_valid(uint32_t len, size_t cap) {
    if (len == 0 || len >= cap || len > MAX_PATH_LEN) return 0; /* invalid */
    return 1; /* valid */
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: zero length — always invalid */
void test_zero_length_rejected(void) {
    TEST_ASSERT_EQUAL_INT(0, recv_length_valid(0, 512));
}

/* Test 2: normal filename length well within all bounds */
void test_normal_length_accepted(void) {
    TEST_ASSERT_EQUAL_INT(1, recv_length_valid(9, 512)); /* "hello.txt" */
}

/* Test 3: exactly MAX_PATH_LEN — accepted (boundary value) */
void test_max_path_len_accepted(void) {
    TEST_ASSERT_EQUAL_INT(1, recv_length_valid(MAX_PATH_LEN, MAX_PATH_LEN + 2));
}

/* Test 4: one over MAX_PATH_LEN — rejected */
void test_over_max_path_len_rejected(void) {
    TEST_ASSERT_EQUAL_INT(0, recv_length_valid(MAX_PATH_LEN + 1, MAX_PATH_LEN + 2));
}

/* Test 5: length == cap — rejected (>= cap would overflow buffer on null terminator) */
void test_length_equal_cap_rejected(void) {
    TEST_ASSERT_EQUAL_INT(0, recv_length_valid(512, 512));
}

/* Test 6: length == cap-1 — accepted (largest value that fits with null terminator) */
void test_length_one_below_cap_accepted(void) {
    TEST_ASSERT_EQUAL_INT(1, recv_length_valid(511, 512));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_length_rejected);
    RUN_TEST(test_normal_length_accepted);
    RUN_TEST(test_max_path_len_accepted);
    RUN_TEST(test_over_max_path_len_rejected);
    RUN_TEST(test_length_equal_cap_rejected);
    RUN_TEST(test_length_one_below_cap_accepted);
    return UNITY_END();
}
