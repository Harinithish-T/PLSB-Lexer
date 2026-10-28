#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/input_buffer.h"
#include "../../src/lexer.h"
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

static void test_baseline_vs_plsb_equivalence(void) {
    const char *source = 
        "/* Mathematical and compiler test code */\n"
        "#include <stdio.h>\n"
        "int compute_value(int alpha, double beta) {\n"
        "    char *msg = \"Processing buffer test with escape \\\"quote\\\" and \\\\ slash\";\n"
        "    if (alpha <= 0 && beta >= 100.5) {\n"
        "        return -1;\n"
        "    }\n"
        "    // Single line comment right here\n"
        "    alpha += 10;\n"
        "    return alpha << 2;\n"
        "}\n";
    
    size_t test_buffer_sizes[] = {2, 7, 16, 32, 64, 128, 512, 4096};
    size_t num_sizes = sizeof(test_buffer_sizes) / sizeof(test_buffer_sizes[0]);
    
    for (size_t s = 0; s < num_sizes; ++s) {
        size_t bsize = test_buffer_sizes[s];
        
        /* 1. Run Baseline */
        input_buffer_t ib_b;
        input_buffer_init_string(&ib_b, source, strlen(source), bsize);
        statistics_t stats_b;
        statistics_init(&stats_b);
        lexer_t lex_b;
        lexer_init(&lex_b, &ib_b, SPILL_POLICY_BASELINE, 16, NULL, &stats_b, NULL);
        
        token_t b_toks[256];
        size_t b_cnt = 0;
        while (1) {
            token_t t;
            lexer_next_token(&lex_b, &t);
            if (t.type == TOKEN_EOF) {
                token_free(&t);
                break;
            }
            b_toks[b_cnt++] = t;
        }
        
        /* 2. Run PLSB */
        input_buffer_t ib_p;
        input_buffer_init_string(&ib_p, source, strlen(source), bsize);
        predictor_t pred;
        predictor_init_default(&pred);
        statistics_t stats_p;
        statistics_init(&stats_p);
        lexer_t lex_p;
        lexer_init(&lex_p, &ib_p, SPILL_POLICY_PLSB, 16, &pred, &stats_p, NULL);
        
        token_t p_toks[256];
        size_t p_cnt = 0;
        while (1) {
            token_t t;
            lexer_next_token(&lex_p, &t);
            if (t.type == TOKEN_EOF) {
                token_free(&t);
                break;
            }
            p_toks[p_cnt++] = t;
        }
        
        /* Verify identical token count */
        ASSERT_TEST(b_cnt == p_cnt, "Token counts must be identical between Baseline and PLSB");
        
        /* Verify every single token matches in type, lexeme, line, and column */
        bool exact_match = true;
        for (size_t i = 0; i < b_cnt && i < p_cnt; ++i) {
            if (b_toks[i].type != p_toks[i].type ||
                b_toks[i].line != p_toks[i].line ||
                b_toks[i].column != p_toks[i].column ||
                strcmp(b_toks[i].lexeme, p_toks[i].lexeme) != 0) {
                exact_match = false;
                break;
            }
        }
        ASSERT_TEST(exact_match, "Every token matches identically between Baseline and PLSB");
        
        /* Cleanup */
        for (size_t i = 0; i < b_cnt; ++i) token_free(&b_toks[i]);
        for (size_t i = 0; i < p_cnt; ++i) token_free(&p_toks[i]);
        lexer_free(&lex_b);
        lexer_free(&lex_p);
        input_buffer_free(&ib_b);
        input_buffer_free(&ib_p);
    }
}

#ifdef STANDALONE_TEST
int main(void) {
#else
int run_test_comparison(void) {
#endif
    printf("[TEST] Running Integration Tests: Equivalence Verification (Baseline vs PLSB)...\n");
    test_baseline_vs_plsb_equivalence();
    
    printf("  Results: %d/%d tests passed (%d failures)\n\n", 
           g_tests_passed, g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
