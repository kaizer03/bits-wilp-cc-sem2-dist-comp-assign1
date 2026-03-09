/*
 * test_file_identical.c
 * ----------------------
 * Unit tests for file_is_identical_in_list() — the pure-logic function that
 * checks whether a file entry (identified by name AND size) has an identical
 * counterpart in another server's file list.
 *
 * This function drives the "(identical)" annotation in the `list file`
 * command output, which is part of the original problem statement's
 * requirement to detect identical vs diverged copies across the two servers.
 *
 * Test cases:
 *   1. File with matching name and size is found → 1
 *   2. File with matching name but different size is not identical → 0
 *   3. File with different name but matching size is not identical → 0
 *   4. File not present in the other list at all → 0
 *   5. Empty other list → 0
 *   6. Multiple entries in other list — match on second entry → 1
 *
 * Run via:  make test
 */

#include "unity.h"
#include <string.h>
#include <stdint.h>

/* FileEntry definition — mirrors client.c */
typedef struct {
    char name[512];
    uint32_t size;
} FileEntry;

/* Function under test — copied from client.c */
static int file_is_identical_in_list(const FileEntry *src, int idx,
                                     const FileEntry *other, int other_count) {
    for (int k = 0; k < other_count; k++) {
        if (strcmp(src[idx].name, other[k].name) == 0 &&
            src[idx].size == other[k].size) {
            return 1;
        }
    }
    return 0;
}

void setUp(void) {}
void tearDown(void) {}

/* Test 1: exact name and size match → identical */
void test_match_name_and_size(void) {
    FileEntry src[1]   = { { "hello.txt", 100 } };
    FileEntry other[1] = { { "hello.txt", 100 } };
    TEST_ASSERT_EQUAL_INT(1, file_is_identical_in_list(src, 0, other, 1));
}

/* Test 2: name matches but size differs → not identical */
void test_same_name_different_size(void) {
    FileEntry src[1]   = { { "hello.txt", 100 } };
    FileEntry other[1] = { { "hello.txt", 200 } };
    TEST_ASSERT_EQUAL_INT(0, file_is_identical_in_list(src, 0, other, 1));
}

/* Test 3: size matches but name differs → not identical */
void test_different_name_same_size(void) {
    FileEntry src[1]   = { { "hello.txt", 100 } };
    FileEntry other[1] = { { "world.txt", 100 } };
    TEST_ASSERT_EQUAL_INT(0, file_is_identical_in_list(src, 0, other, 1));
}

/* Test 4: file absent from other list → not identical */
void test_file_not_in_other_list(void) {
    FileEntry src[1]   = { { "hello.txt", 100 } };
    FileEntry other[1] = { { "other.txt", 50  } };
    TEST_ASSERT_EQUAL_INT(0, file_is_identical_in_list(src, 0, other, 1));
}

/* Test 5: empty other list → not identical */
void test_empty_other_list(void) {
    FileEntry src[1] = { { "hello.txt", 100 } };
    TEST_ASSERT_EQUAL_INT(0, file_is_identical_in_list(src, 0, NULL, 0));
}

/* Test 6: match is the second entry in the other list */
void test_match_second_entry(void) {
    FileEntry src[1]   = { { "hello.txt", 100 } };
    FileEntry other[2] = { { "alpha.txt", 50 }, { "hello.txt", 100 } };
    TEST_ASSERT_EQUAL_INT(1, file_is_identical_in_list(src, 0, other, 2));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_match_name_and_size);
    RUN_TEST(test_same_name_different_size);
    RUN_TEST(test_different_name_same_size);
    RUN_TEST(test_file_not_in_other_list);
    RUN_TEST(test_empty_other_list);
    RUN_TEST(test_match_second_entry);
    return UNITY_END();
}
