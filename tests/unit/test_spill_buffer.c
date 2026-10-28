#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/spill_buffer.h"
#include "../../src/predictor.h"
#include "../../src/statistics.h"

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

static void test_baseline_geometric_growth(void) {
    statistics_t stats;
    statistics_init(&stats);
    
    spill_buffer_t sb;
    spill_buffer_init(&sb, SPILL_POLICY_BASELINE, 16, NULL, &stats);
    
    /* Token starts spilling with 5 bytes prefix */
    spill_buffer_begin(&sb, TOKEN_CLASS_STRING, 5);
    ASSERT_TEST(sb.capacity == 16, "Initial baseline capacity is 16");
    ASSERT_TEST(stats.spill_allocations == 1, "Recorded 1 initial allocation");
    ASSERT_TEST(stats.lexeme_spill_events == 1, "Recorded 1 spill event");
    
    /* Append 10 bytes: total length = 10, fits within 16 (10 + 1 <= 16) */
    spill_buffer_append(&sb, "0123456789", 10);
    ASSERT_TEST(sb.length == 10, "Length is 10");
    ASSERT_TEST(stats.spill_reallocations == 0, "No realloc yet");
    ASSERT_TEST(stats.growth_copy_bytes == 0, "No growth copy bytes yet");
    
    /* Append another 10 bytes: total length = 20, exceeds 16 -> geometric doubling to 32 */
    spill_buffer_append(&sb, "ABCDEFGHIJ", 10);
    ASSERT_TEST(sb.length == 20, "Length is 20");
    ASSERT_TEST(sb.capacity == 32, "Doubled to 32");
    ASSERT_TEST(stats.spill_reallocations == 1, "Recorded 1 reallocation");
    ASSERT_TEST(stats.growth_copy_bytes == 10, "Copied existing 10 bytes during growth");
    
    /* Finish token */
    size_t final_len = 0;
    char *result = spill_buffer_finish(&sb, &final_len);
    ASSERT_TEST(final_len == 20, "Finished token length is 20");
    ASSERT_TEST(strcmp(result, "0123456789ABCDEFGHIJ") == 0, "Contents intact");
    free(result);
    spill_buffer_free(&sb);
}

static void test_plsb_prediction_hit(void) {
    statistics_t stats;
    statistics_init(&stats);
    
    predictor_t pred;
    predictor_init_default(&pred);
    
    /* Train predictor with 60-byte string literals */
    for (int i = 0; i < 10; ++i) {
        predictor_update(&pred, TOKEN_CLASS_STRING, 60);
    }
    
    spill_buffer_t sb;
    spill_buffer_init(&sb, SPILL_POLICY_PLSB, 16, &pred, &stats);
    
    /* Begin spill for a string with 20 bytes initially */
    spill_buffer_begin(&sb, TOKEN_CLASS_STRING, 20);
    ASSERT_TEST(sb.capacity >= 60, "PLSB reserved predicted capacity >= 60");
    ASSERT_TEST(stats.prediction_triggers == 1, "Recorded 1 prediction trigger");
    
    /* Append total 50 bytes -> fits in predicted capacity without realloc! */
    char chunk[50];
    memset(chunk, 'x', 50);
    spill_buffer_append(&sb, chunk, 50);
    
    ASSERT_TEST(stats.spill_reallocations == 0, "Zero reallocations occurred (Prediction Hit!)");
    ASSERT_TEST(stats.growth_copy_bytes == 0, "Zero growth copy bytes");
    
    size_t final_len = 0;
    char *result = spill_buffer_finish(&sb, &final_len);
    ASSERT_TEST(final_len == 50, "Final length is 50");
    ASSERT_TEST(stats.prediction_hits == 1, "Recorded 1 prediction hit");
    ASSERT_TEST(stats.prediction_misses == 0, "Recorded 0 prediction misses");
    ASSERT_TEST(stats.over_reservation_bytes > 0, "Recorded over-reservation bytes");
    
    free(result);
    spill_buffer_free(&sb);
}

#ifdef STANDALONE_TEST
int main(void) {
#else
int run_test_spill_buffer(void) {
#endif
    printf("[TEST] Running Unit Tests: Spill Buffer...\n");
    test_baseline_geometric_growth();
    test_plsb_prediction_hit();
    
    printf("  Results: %d/%d tests passed (%d failures)\n\n", 
           g_tests_passed, g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
