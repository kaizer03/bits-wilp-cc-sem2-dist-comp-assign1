/*
 * test_target_label.c
 * --------------------
 * Unit tests for the target-to-label ternary chains used throughout the
 * client and both servers to produce human-readable scope descriptions.
 *
 * Three distinct label sets are tested:
 *
 * A) Client command target label (cmd_create, cmd_list in client.c):
 *      TARGET_BOTH (0) → "both servers"
 *      TARGET_S1   (1) → "Server 1"
 *      TARGET_S2   (2) → "Server 2"
 *
 * B) Server1 create/list target label (handle_create, handle_list in server1.c):
 *      CREATE_BOTH / LIST_BOTH (0) → "both servers"
 *      CREATE_S1   / LIST_S1   (1) → "SERVER1 only"
 *      CREATE_S2   / LIST_S2   (2) → "SERVER2 only"
 *
 * C) Server1 delete scope label (handle_delete in server1.c):
 *      0 → "both servers"
 *      1 → "SERVER1 only"
 *      2 → "SERVER2 only"
 *      any other value → "cancelled"
 *
 * Test cases:
 *   Client label:  both, S1, S2
 *   Server label:  both, S1, S2
 *   Delete scope:  both, S1, S2, out-of-range (cancelled)
 *   Total: 10
 *
 * Run via:  make test
 */

#include "unity.h"
#include <stdint.h>
#include <string.h>

/* Constants — mirrored from client.c and server1.c */
#define TARGET_BOTH  0
#define TARGET_S1    1
#define TARGET_S2    2

#define CREATE_BOTH  0
#define CREATE_S1    1
#define CREATE_S2    2

/* Label functions — each mirrors an inline ternary from the source files */

static const char *client_target_label(uint8_t target) {
    return (target == TARGET_BOTH) ? "both servers"
         : (target == TARGET_S1)   ? "Server 1"
         :                           "Server 2";
}

static const char *server_target_label(uint8_t target) {
    return (target == CREATE_BOTH) ? "both servers"
         : (target == CREATE_S1)   ? "SERVER1 only"
         :                           "SERVER2 only";
}

static const char *delete_scope_label(uint8_t del_target) {
    return (del_target == 0) ? "both servers"
         : (del_target == 1) ? "SERVER1 only"
         : (del_target == 2) ? "SERVER2 only"
         :                     "cancelled";
}

void setUp(void) {}
void tearDown(void) {}

/* ---- Client target label ---- */

void test_client_label_both(void) {
    TEST_ASSERT_EQUAL_STRING("both servers", client_target_label(TARGET_BOTH));
}

void test_client_label_s1(void) {
    TEST_ASSERT_EQUAL_STRING("Server 1", client_target_label(TARGET_S1));
}

void test_client_label_s2(void) {
    TEST_ASSERT_EQUAL_STRING("Server 2", client_target_label(TARGET_S2));
}

/* ---- Server target label (create / list) ---- */

void test_server_label_both(void) {
    TEST_ASSERT_EQUAL_STRING("both servers", server_target_label(CREATE_BOTH));
}

void test_server_label_s1(void) {
    TEST_ASSERT_EQUAL_STRING("SERVER1 only", server_target_label(CREATE_S1));
}

void test_server_label_s2(void) {
    TEST_ASSERT_EQUAL_STRING("SERVER2 only", server_target_label(CREATE_S2));
}

/* ---- Delete scope label ---- */

void test_delete_scope_both(void) {
    TEST_ASSERT_EQUAL_STRING("both servers", delete_scope_label(0));
}

void test_delete_scope_s1(void) {
    TEST_ASSERT_EQUAL_STRING("SERVER1 only", delete_scope_label(1));
}

void test_delete_scope_s2(void) {
    TEST_ASSERT_EQUAL_STRING("SERVER2 only", delete_scope_label(2));
}

void test_delete_scope_cancelled(void) {
    TEST_ASSERT_EQUAL_STRING("cancelled", delete_scope_label(99));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_client_label_both);
    RUN_TEST(test_client_label_s1);
    RUN_TEST(test_client_label_s2);
    RUN_TEST(test_server_label_both);
    RUN_TEST(test_server_label_s1);
    RUN_TEST(test_server_label_s2);
    RUN_TEST(test_delete_scope_both);
    RUN_TEST(test_delete_scope_s1);
    RUN_TEST(test_delete_scope_s2);
    RUN_TEST(test_delete_scope_cancelled);
    return UNITY_END();
}
