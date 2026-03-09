/*
 * test_server_ready.c
 * --------------------
 * Unit tests for the server-readiness boolean expression used inside
 * wait_for_servers() in client.c:
 *
 *   s1_ready = !need_s1 || s1;
 *   s2_ready = !need_s2 || s2;
 *
 * A server counts as "ready" either because we do not need it at all
 * (e.g. recovering only S1 when S2 is already up), OR because it is
 * actually online. This expression drives the polling loop that gates
 * all auto-recovery flows.
 *
 * Test cases:
 *   1. Server not needed and offline   → ready   (don't need it)
 *   2. Server not needed and online    → ready   (don't need it)
 *   3. Server needed but still offline → not ready
 *   4. Server needed and online        → ready
 *
 * Run via:  make test
 */

#include "unity.h"

/* Expression under test — copied verbatim from client.c wait_for_servers() */
static int server_ready(int need, int up) {
    return !need || up;
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: not needed, offline → ready */
void test_not_needed_offline_is_ready(void) {
    TEST_ASSERT_EQUAL_INT(1, server_ready(0, 0));
}

/* Test 2: not needed, online → ready */
void test_not_needed_online_is_ready(void) {
    TEST_ASSERT_EQUAL_INT(1, server_ready(0, 1));
}

/* Test 3: needed but offline → not ready */
void test_needed_offline_not_ready(void) {
    TEST_ASSERT_EQUAL_INT(0, server_ready(1, 0));
}

/* Test 4: needed and online → ready */
void test_needed_online_is_ready(void) {
    TEST_ASSERT_EQUAL_INT(1, server_ready(1, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_not_needed_offline_is_ready);
    RUN_TEST(test_not_needed_online_is_ready);
    RUN_TEST(test_needed_offline_not_ready);
    RUN_TEST(test_needed_online_is_ready);
    return UNITY_END();
}
