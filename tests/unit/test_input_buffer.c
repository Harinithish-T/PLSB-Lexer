#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../src/input_buffer.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define ASSERT_TEST(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while(0)

static void test_empty_string_buffer(void) {
    input_buffer_t ib;
    int rc = input_buffer_init_string(&ib, "", 0, 16);
    ASSERT_TEST(rc == 0, "Init empty string should succeed");
    ASSERT_TEST(ib.bytes_read[0] == 0, "Empty string should have 0 bytes in buffer 0");
    ASSERT_TEST(ib.buffers[0][0] == '\0', "Sentinel must be placed at index 0");
    ASSERT_TEST(ib.eof_reached == true, "Empty string should signal EOF immediately");
    input_buffer_free(&ib);
}

static void test_single_byte_buffer(void) {
    input_buffer_t ib;
    const char *text = "XYZ";
    int rc = input_buffer_init_string(&ib, text, 3, 1);
    ASSERT_TEST(rc == 0, "Init 1-byte buffer should succeed");
    ASSERT_TEST(ib.buffer_size == 1, "Buffer size should be 1");
    ASSERT_TEST(ib.bytes_read[0] == 1, "First refill should read 1 byte");
    ASSERT_TEST(ib.buffers[0][0] == 'X', "First byte must be X");
    ASSERT_TEST(ib.buffers[0][1] == '\0', "Sentinel must follow first byte");
    
    /* Refill buffer 1 */
    size_t n = input_buffer_refill(&ib, 1);
    ASSERT_TEST(n == 1, "Refill buffer 1 should read 1 byte");
    ASSERT_TEST(ib.buffers[1][0] == 'Y', "Second byte must be Y");
    ASSERT_TEST(ib.buffers[1][1] == '\0', "Sentinel must follow Y");
    
    /* Refill buffer 0 */
    n = input_buffer_refill(&ib, 0);
    ASSERT_TEST(n == 1, "Refill buffer 0 should read 1 byte");
    ASSERT_TEST(ib.buffers[0][0] == 'Z', "Third byte must be Z");
    
    /* Refill buffer 1 at EOF */
    n = input_buffer_refill(&ib, 1);
    ASSERT_TEST(n == 0, "EOF refill should return 0 bytes");
    ASSERT_TEST(ib.eof_reached == true, "EOF flag should be set");
    
    input_buffer_free(&ib);
}

static void test_alternating_refill_data_integrity(void) {
    input_buffer_t ib;
    const char *data = "abcdefghijklmnop"; /* 16 bytes */
    int rc = input_buffer_init_string(&ib, data, 16, 4);
    ASSERT_TEST(rc == 0, "Init with 4-byte buffer");
    
    char assembled[32] = {0};
    size_t pos = 0;
    int current = 0;
    
    while (!ib.eof_reached || ib.bytes_read[current] > 0) {
        memcpy(assembled + pos, ib.buffers[current], ib.bytes_read[current]);
        pos += ib.bytes_read[current];
        if (ib.eof_reached) break;
        current = 1 - current;
        input_buffer_refill(&ib, current);
    }
    
    ASSERT_TEST(pos == 16, "All 16 bytes read");
    ASSERT_TEST(memcmp(assembled, data, 16) == 0, "Reassembled stream matches original data exactly");
    input_buffer_free(&ib);
}

static void test_embedded_nul_handling(void) {
    input_buffer_t ib;
    char text[6] = {'a', '\0', 'b', 'c', '\0', 'd'};
    int rc = input_buffer_init_string(&ib, text, 6, 6);
    ASSERT_TEST(rc == 0, "Init with embedded NUL");
    ASSERT_TEST(ib.bytes_read[0] == 6, "All 6 bytes in buffer 0");
    ASSERT_TEST(ib.buffers[0][1] == '\0', "Index 1 is embedded NUL");
    ASSERT_TEST(ib.buffers[0][6] == '\0', "Index 6 is buffer sentinel");
    /* The buffer boundary check (forward == buffer + bytes_read) distinguishes index 1 from index 6 */
    ASSERT_TEST((ib.buffers[0] + 1) != (ib.buffers[0] + ib.bytes_read[0]), 
                "Embedded NUL position differs from boundary sentinel position");
    input_buffer_free(&ib);
}

#ifdef STANDALONE_TEST
int main(void) {
#else
int run_test_input_buffer(void) {
#endif
    printf("[TEST] Running Unit Tests: Input Buffer...\n");
    test_empty_string_buffer();
    test_single_byte_buffer();
    test_alternating_refill_data_integrity();
    test_embedded_nul_handling();
    
    printf("  Results: %d/%d tests passed (%d failures)\n\n", 
           g_tests_passed, g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
