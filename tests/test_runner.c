#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/input_buffer.h"
#include "../src/lexer.h"
#include "../src/predictor.h"
#include "../src/spill_buffer.h"
#include "../src/statistics.h"
#include "../src/report.h"

static int g_total_tests = 0;
static int g_total_passed = 0;
static int g_total_failed = 0;

#define TEST_CHECK(expr, desc) do { \
    g_total_tests++; \
    if (expr) { \
        g_total_passed++; \
        printf("  [PASS] %s\n", desc); \
    } else { \
        g_total_failed++; \
        printf("  [FAIL] %s (Line %d)\n", desc, __LINE__); \
    } \
} while(0)

static void run_fixture_tests(void) {
    printf("\n=== RUNNING FIXTURE TESTS ===\n");
    
    /* 1. Empty file */
    {
        input_buffer_t ib;
        int rc = input_buffer_init_file(&ib, "tests/fixtures/empty.c", 64);
        TEST_CHECK(rc == 0, "Open tests/fixtures/empty.c");
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        token_t tok;
        lexer_next_token(&l, &tok);
        TEST_CHECK(tok.type == TOKEN_EOF, "Empty file yields TOKEN_EOF immediately");
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 2. Whitespace-only file */
    {
        input_buffer_t ib;
        int rc = input_buffer_init_file(&ib, "tests/fixtures/whitespace_only.c", 4);
        TEST_CHECK(rc == 0, "Open tests/fixtures/whitespace_only.c");
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        token_t tok;
        lexer_next_token(&l, &tok);
        TEST_CHECK(tok.type == TOKEN_EOF, "Whitespace file yields TOKEN_EOF after skipping spaces/newlines");
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 3. Sample C file - Baseline vs PLSB equivalence */
    {
        size_t bsize = 16; /* small buffer to trigger many spill events */
        
        input_buffer_t ib_b;
        input_buffer_init_file(&ib_b, "tests/fixtures/sample_c.c", bsize);
        statistics_t stats_b;
        statistics_init(&stats_b);
        lexer_t lex_b;
        lexer_init(&lex_b, &ib_b, SPILL_POLICY_BASELINE, 16, NULL, &stats_b, NULL);
        
        token_t b_toks[512];
        size_t b_cnt = 0;
        while (1) {
            token_t t;
            lexer_next_token(&lex_b, &t);
            if (t.type == TOKEN_EOF) { token_free(&t); break; }
            b_toks[b_cnt++] = t;
        }
        
        input_buffer_t ib_p;
        input_buffer_init_file(&ib_p, "tests/fixtures/sample_c.c", bsize);
        predictor_t pred;
        predictor_init_default(&pred);
        statistics_t stats_p;
        statistics_init(&stats_p);
        lexer_t lex_p;
        lexer_init(&lex_p, &ib_p, SPILL_POLICY_PLSB, 16, &pred, &stats_p, NULL);
        
        token_t p_toks[512];
        size_t p_cnt = 0;
        while (1) {
            token_t t;
            lexer_next_token(&lex_p, &t);
            if (t.type == TOKEN_EOF) { token_free(&t); break; }
            p_toks[p_cnt++] = t;
        }
        
        TEST_CHECK(b_cnt == p_cnt, "sample_c.c: Token counts identical between Baseline and PLSB");
        
        bool all_matched = true;
        for (size_t i = 0; i < b_cnt && i < p_cnt; ++i) {
            if (b_toks[i].type != p_toks[i].type ||
                b_toks[i].line != p_toks[i].line ||
                b_toks[i].column != p_toks[i].column ||
                strcmp(b_toks[i].lexeme, p_toks[i].lexeme) != 0) {
                all_matched = false;
                break;
            }
        }
        TEST_CHECK(all_matched, "sample_c.c: All emitted tokens 100% identical");
        TEST_CHECK(stats_b.input_refill_count == stats_p.input_refill_count, 
                   "sample_c.c: Refill count identical (PLSB does not distort buffer refills)");
        TEST_CHECK(stats_b.lexeme_spill_events == stats_p.lexeme_spill_events,
                   "sample_c.c: Lexeme spill event counts identical");
        
        for (size_t i = 0; i < b_cnt; ++i) token_free(&b_toks[i]);
        for (size_t i = 0; i < p_cnt; ++i) token_free(&p_toks[i]);
        lexer_free(&lex_b);
        lexer_free(&lex_p);
        input_buffer_free(&ib_b);
        input_buffer_free(&ib_p);
    }
    
    /* 4. Stress long tokens file */
    {
        input_buffer_t ib;
        input_buffer_init_file(&ib, "tests/fixtures/stress_long_tokens.c", 32);
        predictor_t pred;
        predictor_init_default(&pred);
        statistics_t stats;
        statistics_init(&stats);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_PLSB, 16, &pred, &stats, NULL);
        
        size_t count = 0;
        while (1) {
            token_t t;
            lexer_next_token(&l, &t);
            if (t.type == TOKEN_EOF) { token_free(&t); break; }
            count++;
            token_free(&t);
        }
        TEST_CHECK(count > 0, "stress_long_tokens.c parsed completely");
        TEST_CHECK(stats.lexeme_spill_events > 0, "Stress file triggered lexeme spill events as intended");
        TEST_CHECK(stats.prediction_triggers > 0, "Stress file triggered PLSB predictions");
        lexer_free(&l);
        input_buffer_free(&ib);
    }
}

/* External suite runners */
int run_test_input_buffer(void);
int run_test_predictor(void);
int run_test_spill_buffer(void);
int run_test_lexer(void);
int run_test_boundary_conditions(void);
int run_test_comparison(void);
int run_test_regression(void);

int main(void) {
    printf("======================================================================\n");
    printf("  PLSB LEXER - MASTER COMPREHENSIVE TEST SUITE RUNNER\n");
    printf("======================================================================\n\n");
    
    int suites_failed = 0;
    
    if (run_test_input_buffer() != 0) suites_failed++;
    if (run_test_predictor() != 0) suites_failed++;
    if (run_test_spill_buffer() != 0) suites_failed++;
    if (run_test_lexer() != 0) suites_failed++;
    if (run_test_boundary_conditions() != 0) suites_failed++;
    if (run_test_comparison() != 0) suites_failed++;
    if (run_test_regression() != 0) suites_failed++;
    
    run_fixture_tests();
    if (g_total_failed > 0) suites_failed++;
    
    printf("\n======================================================================\n");
    printf("MASTER TEST SUITE SUMMARY:\n");
    printf("  Fixture Checks:  %d Passed, %d Failed (Total %d checks)\n", 
           g_total_passed, g_total_failed, g_total_tests);
    printf("  Suites Status:   %s (%d suites failed)\n",
           (suites_failed == 0) ? "ALL 8 SUITES PASSED" : "FAILURES DETECTED", suites_failed);
    printf("======================================================================\n");
    
    return (suites_failed == 0) ? 0 : 1;
}
