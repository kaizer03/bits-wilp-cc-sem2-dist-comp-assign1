/*
 * test_server_status_code.c
 * --------------------------
 * Unit tests for server_status_code() — the pure-logic function that maps
 * the two server up/down booleans returned by an OP_PING round-trip into a
 * single integer status code used throughout the client's recovery engine.
 *
 * Status code contract:
 *   3 = both Server 1 and Server 2 are online
 *   1 = Server 1 online, Server 2 offline (or unknown)
 *   0 = Server 1 offline (Server 2 status is unknown)
 *
 * Test cases:
 *   1. Both servers online  → 3
 *   2. S1 online, S2 down   → 1
 *   3. S1 down, S2 online   → 0  (S2 can only be known via S1; if S1 is down
 *                                  its report of S2=1 is stale/unreachable)
 *   4. Both servers offline → 0
 *
 * Run via:  make test
 */

#include "unity.h"

/* Function under test — copied from client.c */
static int server_status_code(int s1, int s2) {
    if (s1 && s2) return 3;
    if (s1 && !s2) return 1;
    return 0;
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: both online → 3 */
void test_both_online(void) {
    TEST_ASSERT_EQUAL_INT(3, server_status_code(1, 1));
}

/* Test 2: S1 online, S2 offline → 1 */
void test_s1_online_s2_offline(void) {
    TEST_ASSERT_EQUAL_INT(1, server_status_code(1, 0));
}

/* Test 3: S1 offline, S2 reported online → 0 (S1 is the gatekeeper) */
void test_s1_offline_s2_online(void) {
    TEST_ASSERT_EQUAL_INT(0, server_status_code(0, 1));
}

/* Test 4: both offline → 0 */
void test_both_offline(void) {
    TEST_ASSERT_EQUAL_INT(0, server_status_code(0, 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_both_online);
    RUN_TEST(test_s1_online_s2_offline);
    RUN_TEST(test_s1_offline_s2_online);
    RUN_TEST(test_both_offline);
    return UNITY_END();
}
