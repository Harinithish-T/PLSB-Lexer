#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../src/input_buffer.h"
#include "../../src/lexer.h"
#include "../../src/spill_buffer.h"
#include "../../src/predictor.h"
#include "../../src/statistics.h"
#include "../../src/token_array.h"

static int g_assert_count = 0;
static int g_assert_passed = 0;
static int g_assert_failed = 0;
static int g_cases_count = 0;
static int g_cases_passed = 0;
static int g_cases_failed = 0;

#define REG_ASSERT(cond, msg) do { \
    g_assert_count++; \
    if (cond) { \
        g_assert_passed++; \
    } else { \
        g_assert_failed++; \
        printf("    [FAIL ASSERT] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

#define START_TEST_CASE(name) do { \
    g_cases_count++; \
    int prev_fails = g_assert_failed; \
    const char *case_name = name; \
    printf("  [CASE %d] %s...\n", g_cases_count, case_name);

#define END_TEST_CASE() \
    if (g_assert_failed == prev_fails) { \
        g_cases_passed++; \
        printf("    -> PASS\n"); \
    } else { \
        g_cases_failed++; \
        printf("    -> FAIL (%s)\n", case_name); \
    } \
} while (0)

static inline bool token_has_lexeme(const token_t *t, const char *expected) {
    return t != NULL && t->lexeme != NULL && expected != NULL && strcmp(t->lexeme, expected) == 0;
}

/* Helper: lex string completely into tokens array */
static size_t lex_all(const char *src, size_t buf_size, spill_policy_t policy,
                       token_t *out_toks, size_t max_toks) {
    if (!src || !out_toks || max_toks == 0) return 0;
    input_buffer_t ib;
    if (input_buffer_init_string(&ib, src, strlen(src), buf_size) != 0) return 0;
    
    predictor_t pred;
    if (policy == SPILL_POLICY_PLSB) {
        predictor_init_default(&pred);
    }
    
    lexer_t l;
    lexer_init(&l, &ib, policy, 16, (policy == SPILL_POLICY_PLSB) ? &pred : NULL, NULL, NULL);
    
    size_t count = 0;
    while (count < max_toks) {
        token_t t;
        int status = lexer_next_token(&l, &t);
        out_toks[count++] = t;
        if (status <= 0 || t.type == TOKEN_EOF || t.type == TOKEN_ERROR) break;
    }
    
    lexer_free(&l);
    input_buffer_free(&ib);
    return count;
}

static void free_tokens(token_t *toks, size_t count) {
    if (!toks) return;
    for (size_t i = 0; i < count; ++i) {
        token_free(&toks[i]);
    }
}

/* ========================================================================= */
/* 1. Defect 1: Cross-buffer lookahead for 'x ..a' and dot sequences         */
/* ========================================================================= */

static void test_dot_lookahead_reported_case(void) {
    START_TEST_CASE("Reported defect 1: 'x ..a' with buffer size 4");
    
    token_t toks[16];
    size_t n = lex_all("x ..a", 4, SPILL_POLICY_BASELINE, toks, 16);
    
    REG_ASSERT(n == 5, "Emitted 5 tokens (x, ., ., a, EOF)");
    if (n >= 5) {
        REG_ASSERT(toks[0].type == TOKEN_IDENTIFIER && token_has_lexeme(&toks[0], "x"), "Token 0 is identifier 'x'");
        REG_ASSERT(toks[1].type == TOKEN_DOT && token_has_lexeme(&toks[1], "."), "Token 1 is dot '.'");
        REG_ASSERT(toks[2].type == TOKEN_DOT && token_has_lexeme(&toks[2], "."), "Token 2 is dot '.'");
        REG_ASSERT(toks[3].type == TOKEN_IDENTIFIER && token_has_lexeme(&toks[3], "a"), "Token 3 is identifier 'a'");
        REG_ASSERT(toks[4].type == TOKEN_EOF, "Token 4 is EOF");
    }
    free_tokens(toks, n);
    
    /* Also check under PLSB policy */
    n = lex_all("x ..a", 4, SPILL_POLICY_PLSB, toks, 16);
    REG_ASSERT(n == 5, "PLSB emitted 5 tokens (x, ., ., a, EOF)");
    if (n >= 5) {
        REG_ASSERT(toks[0].type == TOKEN_IDENTIFIER && token_has_lexeme(&toks[0], "x"), "PLSB Token 0 is 'x'");
        REG_ASSERT(toks[1].type == TOKEN_DOT && token_has_lexeme(&toks[1], "."), "PLSB Token 1 is '.'");
        REG_ASSERT(toks[2].type == TOKEN_DOT && token_has_lexeme(&toks[2], "."), "PLSB Token 2 is '.'");
        REG_ASSERT(toks[3].type == TOKEN_IDENTIFIER && token_has_lexeme(&toks[3], "a"), "PLSB Token 3 is 'a'");
        REG_ASSERT(toks[4].type == TOKEN_EOF, "PLSB Token 4 is EOF");
    }
    free_tokens(toks, n);
    
    END_TEST_CASE();
}

static void test_dot_sequences_across_buffer_sizes(void) {
    START_TEST_CASE("Dot sequences (., .., ..., ...., ..a) across buffer sizes 1..16 and offsets");
    
    static const size_t BUF_SIZES[] = {1, 2, 3, 4, 5, 7, 16};
    static const size_t NUM_BUFS = sizeof(BUF_SIZES) / sizeof(BUF_SIZES[0]);
    
    static const char *PADS[] = {"", " ", "  ", "   ", "p", "pad"};
    static const size_t NUM_PADS = sizeof(PADS) / sizeof(PADS[0]);
    
    for (size_t b = 0; b < NUM_BUFS; ++b) {
        size_t bsize = BUF_SIZES[b];
        
        for (size_t p = 0; p < NUM_PADS; ++p) {
            char src[64];
            
            /* Test single dot: "." */
            snprintf(src, sizeof(src), "%s.", PADS[p]);
            {
                token_t toks[16];
                size_t n = lex_all(src, bsize, SPILL_POLICY_BASELINE, toks, 16);
                size_t dot_idx = (strlen(PADS[p]) > 0 && PADS[p][0] != ' ') ? 1 : 0;
                REG_ASSERT(n >= dot_idx + 2, "Single dot token count sufficient");
                if (n >= dot_idx + 2) {
                    REG_ASSERT(toks[dot_idx].type == TOKEN_DOT, "Recognized TOKEN_DOT");
                    REG_ASSERT(strcmp(toks[dot_idx].lexeme, ".") == 0, "Lexeme is '.'");
                }
                free_tokens(toks, n);
            }
            
            /* Test double dot: ".." -> two TOKEN_DOT */
            snprintf(src, sizeof(src), "%s..", PADS[p]);
            {
                token_t toks[16];
                size_t n = lex_all(src, bsize, SPILL_POLICY_BASELINE, toks, 16);
                size_t dot_idx = (strlen(PADS[p]) > 0 && PADS[p][0] != ' ') ? 1 : 0;
                REG_ASSERT(n >= dot_idx + 3, "Double dot token count sufficient");
                if (n >= dot_idx + 3) {
                    REG_ASSERT(toks[dot_idx].type == TOKEN_DOT, "1st dot is TOKEN_DOT");
                    REG_ASSERT(toks[dot_idx + 1].type == TOKEN_DOT, "2nd dot is TOKEN_DOT");
                    REG_ASSERT(strcmp(toks[dot_idx].lexeme, ".") == 0, "1st dot lexeme '.'");
                    REG_ASSERT(strcmp(toks[dot_idx + 1].lexeme, ".") == 0, "2nd dot lexeme '.'");
                }
                free_tokens(toks, n);
            }
            
            /* Test ellipsis: "..." -> TOKEN_ELLIPSIS (requires 2-char lookahead, supported where bsize >= 2) */
            if (bsize >= 2) {
                snprintf(src, sizeof(src), "%s...", PADS[p]);
                {
                    token_t toks[16];
                    size_t n = lex_all(src, bsize, SPILL_POLICY_BASELINE, toks, 16);
                    size_t dot_idx = (strlen(PADS[p]) > 0 && PADS[p][0] != ' ') ? 1 : 0;
                    REG_ASSERT(n >= dot_idx + 2, "Ellipsis token count sufficient");
                    if (n >= dot_idx + 2) {
                        REG_ASSERT(toks[dot_idx].type == TOKEN_ELLIPSIS, "Recognized TOKEN_ELLIPSIS");
                        REG_ASSERT(strcmp(toks[dot_idx].lexeme, "...") == 0, "Lexeme is '...'");
                    }
                    free_tokens(toks, n);
                }
                
                /* Test four dots: "...." -> TOKEN_ELLIPSIS followed by TOKEN_DOT */
                snprintf(src, sizeof(src), "%s....", PADS[p]);
                {
                    token_t toks[16];
                    size_t n = lex_all(src, bsize, SPILL_POLICY_BASELINE, toks, 16);
                    size_t dot_idx = (strlen(PADS[p]) > 0 && PADS[p][0] != ' ') ? 1 : 0;
                    REG_ASSERT(n >= dot_idx + 3, "Four dots token count sufficient");
                    if (n >= dot_idx + 3) {
                        REG_ASSERT(toks[dot_idx].type == TOKEN_ELLIPSIS, "Recognized TOKEN_ELLIPSIS");
                        REG_ASSERT(toks[dot_idx + 1].type == TOKEN_DOT, "Followed by TOKEN_DOT");
                        REG_ASSERT(strcmp(toks[dot_idx].lexeme, "...") == 0, "Ellipsis lexeme");
                        REG_ASSERT(strcmp(toks[dot_idx + 1].lexeme, ".") == 0, "Dot lexeme");
                    }
                    free_tokens(toks, n);
                }
            }
            
            /* Test "..a" -> TOKEN_DOT, TOKEN_DOT, TOKEN_IDENTIFIER */
            snprintf(src, sizeof(src), "%s..a", PADS[p]);
            {
                token_t toks[16];
                size_t n = lex_all(src, bsize, SPILL_POLICY_BASELINE, toks, 16);
                size_t dot_idx = (strlen(PADS[p]) > 0 && PADS[p][0] != ' ') ? 1 : 0;
                REG_ASSERT(n >= dot_idx + 4, "'..a' token count sufficient");
                if (n >= dot_idx + 4) {
                    REG_ASSERT(toks[dot_idx].type == TOKEN_DOT, "1st dot is TOKEN_DOT");
                    REG_ASSERT(toks[dot_idx + 1].type == TOKEN_DOT, "2nd dot is TOKEN_DOT");
                    REG_ASSERT(toks[dot_idx + 2].type == TOKEN_IDENTIFIER, "Identifier 'a'");
                    REG_ASSERT(strcmp(toks[dot_idx + 2].lexeme, "a") == 0, "Identifier lexeme is 'a'");
                }
                free_tokens(toks, n);
            }
        }
    }
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 2. Boundary lookahead for all multi-char operators                        */
/* ========================================================================= */

static void test_operator_lookahead_boundaries(void) {
    START_TEST_CASE("Multi-char operators matching and non-matching lookahead across boundaries");
    
    struct {
        const char *src;
        token_type_t expected_type;
        const char *expected_lex;
    } op_cases[] = {
        {"==", TOKEN_OP_EQ, "=="},
        {"!=", TOKEN_OP_NE, "!="},
        {"<=", TOKEN_OP_LE, "<="},
        {">=", TOKEN_OP_GE, ">="},
        {"++", TOKEN_OP_INC, "++"},
        {"--", TOKEN_OP_DEC, "--"},
        {"->", TOKEN_OP_ARROW, "->"},
        {"+=", TOKEN_OP_PLUS_ASSIGN, "+="},
        {"-=", TOKEN_OP_MINUS_ASSIGN, "-="},
        {"*=", TOKEN_OP_STAR_ASSIGN, "*="},
        {"/=", TOKEN_OP_SLASH_ASSIGN, "/="},
        {"%=", TOKEN_OP_PERCENT_ASSIGN, "%="},
        {"&=", TOKEN_OP_BIT_AND_ASSIGN, "&="},
        {"|=", TOKEN_OP_BIT_OR_ASSIGN, "|="},
        {"^=", TOKEN_OP_BIT_XOR_ASSIGN, "^="},
        {"<<", TOKEN_OP_LSHIFT, "<<"},
        {">>", TOKEN_OP_RSHIFT, ">>"},
        {"&&", TOKEN_OP_LOGIC_AND, "&&"},
        {"||", TOKEN_OP_LOGIC_OR, "||"},
        {"##", TOKEN_OP_HASH_HASH, "##"},
        {"<<=", TOKEN_OP_LSHIFT_ASSIGN, "<<="},
        {">>=", TOKEN_OP_RSHIFT_ASSIGN, ">>="}
    };
    
    size_t num_cases = sizeof(op_cases) / sizeof(op_cases[0]);
    size_t buf_sizes[] = {1, 2, 3, 4, 8};
    size_t num_bufs = sizeof(buf_sizes) / sizeof(buf_sizes[0]);
    
    for (size_t b = 0; b < num_bufs; ++b) {
        size_t bsize = buf_sizes[b];
        for (size_t c = 0; c < num_cases; ++c) {
            token_t toks[16];
            size_t n = lex_all(op_cases[c].src, bsize, SPILL_POLICY_BASELINE, toks, 16);
            REG_ASSERT(n >= 2, "Operator token + EOF emitted");
            if (n >= 2) {
                REG_ASSERT(toks[0].type == op_cases[c].expected_type, "Operator type matched");
                REG_ASSERT(strcmp(toks[0].lexeme, op_cases[c].expected_lex) == 0, "Operator lexeme matched");
            }
            free_tokens(toks, n);
        }
    }
    
    END_TEST_CASE();
}

static void test_operator_non_matching_lookahead(void) {
    START_TEST_CASE("Non-matching operator lookaheads (e.g. '=x', '+y', '<z', '- ') across boundaries");
    
    struct {
        const char *src;
        token_type_t t1;
        const char *lex1;
        token_type_t t2;
        const char *lex2;
    } non_match[] = {
        {"=x", TOKEN_OP_ASSIGN, "=", TOKEN_IDENTIFIER, "x"},
        {"+y", TOKEN_OP_PLUS, "+", TOKEN_IDENTIFIER, "y"},
        {"-z", TOKEN_OP_MINUS, "-", TOKEN_IDENTIFIER, "z"},
        {"!w", TOKEN_OP_LOGIC_NOT, "!", TOKEN_IDENTIFIER, "w"},
        {"<q", TOKEN_OP_LT, "<", TOKEN_IDENTIFIER, "q"},
        {">k", TOKEN_OP_GT, ">", TOKEN_IDENTIFIER, "k"},
        {"&m", TOKEN_OP_BIT_AND, "&", TOKEN_IDENTIFIER, "m"},
        {"|n", TOKEN_OP_BIT_OR, "|", TOKEN_IDENTIFIER, "n"},
        {"^p", TOKEN_OP_BIT_XOR, "^", TOKEN_IDENTIFIER, "p"},
        {"#d", TOKEN_OP_HASH, "#", TOKEN_IDENTIFIER, "d"},
        {"<<+", TOKEN_OP_LSHIFT, "<<", TOKEN_OP_PLUS, "+"},
        {">>+", TOKEN_OP_RSHIFT, ">>", TOKEN_OP_PLUS, "+"}
    };
    
    size_t num_cases = sizeof(non_match) / sizeof(non_match[0]);
    size_t buf_sizes[] = {1, 2, 3, 4};
    size_t num_bufs = sizeof(buf_sizes) / sizeof(buf_sizes[0]);
    
    for (size_t b = 0; b < num_bufs; ++b) {
        size_t bsize = buf_sizes[b];
        for (size_t c = 0; c < num_cases; ++c) {
            token_t toks[16];
            size_t n = lex_all(non_match[c].src, bsize, SPILL_POLICY_BASELINE, toks, 16);
            REG_ASSERT(n >= 3, "2 tokens + EOF emitted");
            if (n >= 3) {
                REG_ASSERT(toks[0].type == non_match[c].t1 && strcmp(toks[0].lexeme, non_match[c].lex1) == 0,
                           "First operator recognized");
                REG_ASSERT(toks[1].type == non_match[c].t2 && strcmp(toks[1].lexeme, non_match[c].lex2) == 0,
                           "Second token recognized");
            }
            free_tokens(toks, n);
        }
    }
    
    END_TEST_CASE();
}

static void test_eof_immediately_after_lookahead(void) {
    START_TEST_CASE("EOF immediately after single and multi-character operators");
    
    static const char *EOF_STRINGS[] = {
        ".", "..", "...", "=", "!", "<", "<<", ">", ">>",
        "+", "-", "&", "|", "^", "*", "/", "%", "#", "~", "?"
    };
    size_t num_cases = sizeof(EOF_STRINGS) / sizeof(EOF_STRINGS[0]);
    size_t buf_sizes[] = {1, 2, 3, 4};
    size_t num_bufs = sizeof(buf_sizes) / sizeof(buf_sizes[0]);
    
    for (size_t b = 0; b < num_bufs; ++b) {
        size_t bsize = buf_sizes[b];
        for (size_t c = 0; c < num_cases; ++c) {
            token_t toks[16];
            size_t n = lex_all(EOF_STRINGS[c], bsize, SPILL_POLICY_BASELINE, toks, 16);
            REG_ASSERT(n >= 2, "Emitted valid token followed by EOF");
            if (n >= 1) {
                REG_ASSERT(toks[n - 1].type == TOKEN_EOF, "Final token is EOF");
            }
            free_tokens(toks, n);
        }
    }
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 3. Number literal edge cases and floating dots                            */
/* ========================================================================= */

static void test_number_literals_with_dots(void) {
    START_TEST_CASE("Number literals with dots and ranges (.5, 123.456, 1..5, 0..)");
    
    size_t buf_sizes[] = {1, 2, 3, 4, 8, 16};
    size_t num_bufs = sizeof(buf_sizes) / sizeof(buf_sizes[0]);
    
    for (size_t b = 0; b < num_bufs; ++b) {
        size_t bsize = buf_sizes[b];
        
        /* 1. Leading dot float: ".5" */
        {
            token_t toks[8];
            size_t n = lex_all(".5", bsize, SPILL_POLICY_BASELINE, toks, 8);
            REG_ASSERT(n == 2, "Emitted 2 tokens (.5, EOF)");
            if (n >= 2) {
                REG_ASSERT(toks[0].type == TOKEN_FLOAT_LITERAL, "Type is float");
                REG_ASSERT(strcmp(toks[0].lexeme, ".5") == 0, "Lexeme is '.5'");
            }
            free_tokens(toks, n);
        }
        
        /* 2. Normal float: "123.456" */
        {
            token_t toks[8];
            size_t n = lex_all("123.456", bsize, SPILL_POLICY_BASELINE, toks, 8);
            REG_ASSERT(n == 2, "Emitted 2 tokens (123.456, EOF)");
            if (n >= 2) {
                REG_ASSERT(toks[0].type == TOKEN_FLOAT_LITERAL, "Type is float");
                REG_ASSERT(strcmp(toks[0].lexeme, "123.456") == 0, "Lexeme is '123.456'");
            }
            free_tokens(toks, n);
        }
        
        /* 3. Number followed by range in C: "1..5" -> int '1', dot '.', float '.5', EOF (requires 2-char lookahead, bsize >= 2) */
        if (bsize >= 2) {
            token_t toks[8];
            size_t n = lex_all("1..5", bsize, SPILL_POLICY_BASELINE, toks, 8);
            REG_ASSERT(n == 4, "Emitted 4 tokens (1, ., .5, EOF)");
            if (n >= 4) {
                REG_ASSERT(toks[0].type == TOKEN_INT_LITERAL && strcmp(toks[0].lexeme, "1") == 0, "Token 0 is 1");
                REG_ASSERT(toks[1].type == TOKEN_DOT && strcmp(toks[1].lexeme, ".") == 0, "Token 1 is .");
                REG_ASSERT(toks[2].type == TOKEN_FLOAT_LITERAL && strcmp(toks[2].lexeme, ".5") == 0, "Token 2 is .5");
            }
            free_tokens(toks, n);
        }
        
        /* 4. Integer followed by double dot: "1 .. 5" -> int '1', dot '.', dot '.', int '5', EOF */
        {
            token_t toks[8];
            size_t n = lex_all("1 .. 5", bsize, SPILL_POLICY_BASELINE, toks, 8);
            REG_ASSERT(n == 5, "Emitted 5 tokens (1, ., ., 5, EOF)");
            if (n >= 5) {
                REG_ASSERT(toks[0].type == TOKEN_INT_LITERAL && strcmp(toks[0].lexeme, "1") == 0, "Token 0 is 1");
                REG_ASSERT(toks[1].type == TOKEN_DOT && strcmp(toks[1].lexeme, ".") == 0, "Token 1 is .");
                REG_ASSERT(toks[2].type == TOKEN_DOT && strcmp(toks[2].lexeme, ".") == 0, "Token 2 is .");
                REG_ASSERT(toks[3].type == TOKEN_INT_LITERAL && strcmp(toks[3].lexeme, "5") == 0, "Token 3 is 5");
            }
            free_tokens(toks, n);
        }
        
        /* 5. Zero range: "0.." -> int '0', dot '.', dot '.' (where supported bsize >= 2) */
        if (bsize >= 2) {
            token_t toks[8];
            size_t n = lex_all("0..", bsize, SPILL_POLICY_BASELINE, toks, 8);
            REG_ASSERT(n == 4, "Emitted 4 tokens (0, ., ., EOF)");
            if (n >= 4) {
                REG_ASSERT(toks[0].type == TOKEN_INT_LITERAL && strcmp(toks[0].lexeme, "0") == 0, "Token 0 is 0");
                REG_ASSERT(toks[1].type == TOKEN_DOT && strcmp(toks[1].lexeme, ".") == 0, "Token 1 is .");
                REG_ASSERT(toks[2].type == TOKEN_DOT && strcmp(toks[2].lexeme, ".") == 0, "Token 2 is .");
            }
            free_tokens(toks, n);
        }
    }
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 4. Multi-refill very long tokens                                          */
/* ========================================================================= */

static void test_long_tokens_multi_refill(void) {
    START_TEST_CASE("Tokens spanning multiple buffer refills (length 500 in buffer size 4)");
    
    /* Build identifier of 500 chars */
    char long_id[501];
    memset(long_id, 'a', 500);
    long_id[500] = '\0';
    
    token_t base_tok;
    token_t plsb_tok;
    
    /* Baseline test */
    {
        input_buffer_t ib;
        input_buffer_init_string(&ib, long_id, 500, 4);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        lexer_next_token(&l, &base_tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    REG_ASSERT(base_tok.type == TOKEN_IDENTIFIER, "Baseline parsed long identifier");
    REG_ASSERT(base_tok.length == 500, "Baseline length is 500");
    REG_ASSERT(base_tok.was_spilled == true, "Baseline flagged token as spilled");
    REG_ASSERT(strcmp(base_tok.lexeme, long_id) == 0, "Baseline lexeme content matches exactly");
    
    /* PLSB test */
    {
        input_buffer_t ib;
        input_buffer_init_string(&ib, long_id, 500, 4);
        predictor_t pred;
        predictor_init_default(&pred);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_PLSB, 16, &pred, NULL, NULL);
        lexer_next_token(&l, &plsb_tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    REG_ASSERT(plsb_tok.type == TOKEN_IDENTIFIER, "PLSB parsed long identifier");
    REG_ASSERT(plsb_tok.length == 500, "PLSB length is 500");
    REG_ASSERT(plsb_tok.was_spilled == true, "PLSB flagged token as spilled");
    REG_ASSERT(strcmp(plsb_tok.lexeme, long_id) == 0, "PLSB lexeme content matches exactly");
    
    /* Equivalence check */
    REG_ASSERT(base_tok.type == plsb_tok.type, "Equivalence: token types match");
    REG_ASSERT(base_tok.length == plsb_tok.length, "Equivalence: token lengths match");
    REG_ASSERT(strcmp(base_tok.lexeme, plsb_tok.lexeme) == 0, "Equivalence: exact lexeme bytes match");
    
    token_free(&base_tok);
    token_free(&plsb_tok);
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 5. Deterministic Memory Allocation Failure Injection                      */
/* ========================================================================= */

static int g_malloc_calls = 0;
static int g_fail_after_n_mallocs = -1;

static void *mock_failing_malloc(size_t sz) {
    g_malloc_calls++;
    if (g_fail_after_n_mallocs >= 0 && g_malloc_calls > g_fail_after_n_mallocs) {
        return NULL; /* Injected allocation failure */
    }
    return malloc(sz);
}

static void mock_free(void *ptr) {
    free(ptr);
}

static void test_injected_allocation_failures(void) {
    START_TEST_CASE("Injected allocation failures: initial spill alloc, growth realloc, normal token alloc");
    
    /* 1. Injected failure on initial spill buffer allocation */
    {
        /* Input: "abcdefghij" with buffer_size 4 -> spans boundary, triggers spill_buffer_begin */
        spill_buffer_set_allocator(mock_failing_malloc, mock_free);
        g_malloc_calls = 0;
        g_fail_after_n_mallocs = 0; /* Fail on the very first spill allocation */
        
        input_buffer_t ib;
        input_buffer_init_string(&ib, "abcdefghij", 10, 4);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        
        REG_ASSERT(status == 1, "Lexer returned status 1 on error");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Token type is TOKEN_ERROR");
        REG_ASSERT(tok.lexeme == NULL, "No corrupted lexeme emitted");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "Memory allocation failure") == 0,
                   "Error message is 'Memory allocation failure'");
        
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 2. Injected failure on spill buffer growth/realloc (append expansion) */
    {
        /* Buffer size 4, baseline capacity 16. Spilling a 40-char token will require expansion */
        char long_token[41];
        memset(long_token, 'x', 40);
        long_token[40] = '\0';
        
        spill_buffer_set_allocator(mock_failing_malloc, mock_free);
        g_malloc_calls = 0;
        g_fail_after_n_mallocs = 1; /* First alloc (initial 16 bytes) succeeds; second alloc (growth) fails */
        
        input_buffer_t ib;
        input_buffer_init_string(&ib, long_token, 40, 4);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        
        REG_ASSERT(status == 1, "Lexer returned status 1 on growth allocation failure");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Token type is TOKEN_ERROR");
        REG_ASSERT(tok.lexeme == NULL, "No partial or truncated lexeme emitted");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "Memory allocation failure") == 0,
                   "Error message is 'Memory allocation failure'");
        
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 3. Injected failure on normal (non-spilled) token in finalize_token */
    {
        spill_buffer_set_allocator(mock_failing_malloc, mock_free);
        g_malloc_calls = 0;
        g_fail_after_n_mallocs = 0; /* Fail immediately when copying token lexeme */
        
        input_buffer_t ib;
        input_buffer_init_string(&ib, "varname", 7, 64);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        
        REG_ASSERT(status == 1, "Lexer returned status 1 on normal token copy failure");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Token type is TOKEN_ERROR");
        REG_ASSERT(tok.lexeme == NULL, "Token lexeme is NULL");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "Memory allocation failure") == 0,
                   "Error message is 'Memory allocation failure'");
        
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* Restore standard allocator */
    spill_buffer_set_allocator(malloc, free);
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 6. Arithmetic Overflow Guards                                             */
/* ========================================================================= */

static void test_arithmetic_overflow_guards(void) {
    START_TEST_CASE("Arithmetic overflow guards in spill_buffer and input_buffer");
    
    /* 1. spill_buffer_begin with SIZE_MAX */
    {
        spill_buffer_t sb;
        spill_buffer_init(&sb, SPILL_POLICY_BASELINE, 16, NULL, NULL);
        int rc = spill_buffer_begin(&sb, TOKEN_CLASS_IDENTIFIER, SIZE_MAX);
        REG_ASSERT(rc == -1, "spill_buffer_begin rejects SIZE_MAX initial_bytes");
        spill_buffer_free(&sb);
    }
    
    /* 2. spill_buffer_append with overflowing count */
    {
        spill_buffer_t sb;
        spill_buffer_init(&sb, SPILL_POLICY_BASELINE, 16, NULL, NULL);
        spill_buffer_begin(&sb, TOKEN_CLASS_IDENTIFIER, 16);
        int rc = spill_buffer_append(&sb, "x", SIZE_MAX);
        REG_ASSERT(rc == -1, "spill_buffer_append rejects overflowing count");
        spill_buffer_free(&sb);
    }
    
    /* 3. input_buffer_init_string with buffer_size 0 */
    {
        input_buffer_t ib;
        int rc = input_buffer_init_string(&ib, "abc", 3, 0);
        REG_ASSERT(rc == -1, "input_buffer rejects buffer_size 0");
    }
    
    /* 4. input_buffer_init_string with buffer_size SIZE_MAX */
    {
        input_buffer_t ib;
        int rc = input_buffer_init_string(&ib, "abc", 3, SIZE_MAX);
        REG_ASSERT(rc == -1, "input_buffer rejects buffer_size SIZE_MAX");
    }
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 7. I/O Error Propagation and Cleanup                                      */
/* ========================================================================= */

static void test_io_error_propagation_and_cleanup(void) {
    START_TEST_CASE("I/O error propagation and cleanup on lookahead/refills");
    
    /* 1. I/O error at start of stream */
    {
        input_buffer_t ib;
        input_buffer_init_string(&ib, "var x = 10;", 11, 4);
        ib.io_error = true; /* Simulate I/O read error */
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        REG_ASSERT(status == 1, "Status is 1 on initial I/O error");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Token type is TOKEN_ERROR");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "I/O read error") == 0,
                   "Error message is 'I/O read error'");
        REG_ASSERT(tok.lexeme == NULL, "No corrupted lexeme emitted on I/O error");
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 2. I/O error during operator lookahead (+, -, *, /, =, !, <, >, &, |, ^, #, .) */
    static const char *OPS_TO_TEST[] = {"+", "-", "*", "/", "=", "!", "<", ">", "&", "|", "^", "#", "."};
    size_t num_ops = sizeof(OPS_TO_TEST) / sizeof(OPS_TO_TEST[0]);
    for (size_t i = 0; i < num_ops; ++i) {
        input_buffer_t ib;
        /* Buffer size 1 so consuming first char leaves next char in other buffer */
        input_buffer_init_string(&ib, OPS_TO_TEST[i], strlen(OPS_TO_TEST[i]), 1);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        /* Inject I/O error right before next token peeks beyond buffer */
        ib.io_error = true;
        
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        REG_ASSERT(status == 1, "Operator lookahead status 1 on I/O error");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Operator lookahead emitted TOKEN_ERROR on I/O error");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "I/O read error") == 0,
                   "Operator error message is 'I/O read error'");
        REG_ASSERT(tok.lexeme == NULL, "No corrupted token lexeme on operator I/O error");
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 3. I/O error during multi-refill identifier scanning */
    {
        input_buffer_t ib;
        input_buffer_init_string(&ib, "identifier_text", 15, 1);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        /* Inject I/O error */
        ib.io_error = true;
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        REG_ASSERT(status == 1, "Identifier scanning status 1 on I/O error");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Identifier scanning emitted TOKEN_ERROR");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "I/O read error") == 0,
                   "Identifier error message is 'I/O read error'");
        REG_ASSERT(tok.lexeme == NULL, "No partial identifier emitted");
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    /* 4. I/O error during comment scanning */
    {
        input_buffer_t ib;
        input_buffer_init_string(&ib, "/* block comment */", 19, 2);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        ib.io_error = true;
        token_t tok;
        int status = lexer_next_token(&l, &tok);
        REG_ASSERT(status == 1, "Comment scanning status 1 on I/O error");
        REG_ASSERT(tok.type == TOKEN_ERROR, "Comment emitted TOKEN_ERROR on I/O error");
        REG_ASSERT(tok.error_msg != NULL && strcmp(tok.error_msg, "I/O read error") == 0,
                   "Comment error message is 'I/O read error'");
        token_free(&tok);
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* 8. Token Array Allocation Failure Safety & Overflow Protection            */
/* ========================================================================= */

/* Portable, checked C11-compatible string duplication helper */
static char *c11_strdup(const char *src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, src, len + 1);
    return copy;
}

static void *mock_failing_realloc(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

static void test_token_array_allocation_safety(void) {
    START_TEST_CASE("Token array allocation failure safety, overflow guards, and leak-free cleanup");
    
    /* 1. NULL safety guards */
    {
        token_t dummy;
        memset(&dummy, 0, sizeof(dummy));
        dummy.type = TOKEN_IDENTIFIER;
        
        REG_ASSERT(!token_array_init(NULL, 16), "token_array_init rejects NULL array");
        REG_ASSERT(!token_array_push(NULL, &dummy), "token_array_push rejects NULL array");
        
        token_array_t arr;
        memset(&arr, 0, sizeof(arr));
        REG_ASSERT(!token_array_push(&arr, &dummy), "token_array_push rejects uninitialized array");
        
        REG_ASSERT(token_array_init(&arr, 4), "token_array_init succeeds with capacity 4");
        REG_ASSERT(!token_array_push(&arr, NULL), "token_array_push rejects NULL token pointer");
        token_array_free(&arr);
        REG_ASSERT(arr.tokens == NULL && arr.count == 0 && arr.capacity == 0, "token_array_free resets struct to zeroes");
        token_array_free(NULL); /* Safe no-op */
    }
    
    /* 2. Realloc failure preservation and leak-free recovery */
    {
        token_array_t arr;
        bool ok = token_array_init(&arr, 2);
        REG_ASSERT(ok, "Array initialized with capacity 2");
        
        token_t t1 = { .type = TOKEN_IDENTIFIER, .lexeme = c11_strdup("alpha"), .length = 5, .line = 1, .column = 1 };
        token_t t2 = { .type = TOKEN_IDENTIFIER, .lexeme = c11_strdup("beta"), .length = 4, .line = 1, .column = 7 };
        token_t t3 = { .type = TOKEN_IDENTIFIER, .lexeme = c11_strdup("gamma"), .length = 5, .line = 1, .column = 12 };
        REG_ASSERT(t1.lexeme != NULL && t2.lexeme != NULL && t3.lexeme != NULL, "Test lexemes allocated successfully");
        
        REG_ASSERT(token_array_push(&arr, &t1), "Push token 1 succeeds");
        REG_ASSERT(token_array_push(&arr, &t2), "Push token 2 succeeds (capacity now full)");
        REG_ASSERT(arr.count == 2 && arr.capacity == 2, "Array count is 2 and capacity is 2");
        
        token_t *original_ptr = arr.tokens;
        
        /* Attempt to push 3rd token with injected realloc failure */
        bool pushed = token_array_push_ex(&arr, &t3, mock_failing_realloc);
        REG_ASSERT(!pushed, "Pushing with failing realloc returns false");
        REG_ASSERT(arr.tokens == original_ptr, "Original token buffer pointer preserved intact");
        REG_ASSERT(arr.count == 2, "Token count preserved at 2 on failure");
        REG_ASSERT(arr.capacity == 2, "Capacity preserved at 2 on failure");
        
        /* Ensure previously pushed tokens are still readable and valid */
        REG_ASSERT(arr.tokens[0].type == TOKEN_IDENTIFIER && strcmp(arr.tokens[0].lexeme, "alpha") == 0,
                   "First token preserved after failed push");
        REG_ASSERT(arr.tokens[1].type == TOKEN_IDENTIFIER && strcmp(arr.tokens[1].lexeme, "beta") == 0,
                   "Second token preserved after failed push");
        
        /* Free the array and the unpushed token safely */
        token_array_free(&arr);
        REG_ASSERT(arr.tokens == NULL && arr.count == 0 && arr.capacity == 0, "token_array_free cleaned up array");
        token_free(&t3);
    }
    
    /* 3. Arithmetic overflow guard on capacity doubling */
    {
        token_array_t arr;
        arr.tokens = (token_t *)malloc(sizeof(token_t) * 2);
        arr.capacity = ((SIZE_MAX / 2) / sizeof(token_t)) + 1;
        arr.count = arr.capacity; /* Full capacity forces doubling */
        
        token_t tok = { .type = TOKEN_SEMICOLON, .lexeme = NULL, .length = 1, .line = 1, .column = 1 };
        bool pushed = token_array_push(&arr, &tok);
        REG_ASSERT(!pushed, "token_array_push rejects capacity arithmetic overflow");
        
        free(arr.tokens);
    }
    
    /* 4. Normal dynamic growth and verification */
    {
        token_array_t arr;
        token_array_init(&arr, 2);
        
        for (int i = 0; i < 20; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "tok_%d", i);
            char *tok_str = c11_strdup(buf);
            REG_ASSERT(tok_str != NULL, "Token lexeme allocated successfully during expansion");
            token_t tok = {
                .type = TOKEN_IDENTIFIER,
                .lexeme = tok_str,
                .length = strlen(buf),
                .line = 1,
                .column = (size_t)(i * 6 + 1)
            };
            bool pushed = token_array_push(&arr, &tok);
            REG_ASSERT(pushed, "Normal push succeeded during dynamic expansion");
        }
        
        REG_ASSERT(arr.count == 20, "Total token count is 20");
        REG_ASSERT(arr.capacity >= 20, "Capacity dynamically expanded >= 20");
        
        bool all_valid = true;
        for (int i = 0; i < 20; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "tok_%d", i);
            if (!arr.tokens[i].lexeme || strcmp(arr.tokens[i].lexeme, buf) != 0) {
                all_valid = false;
                break;
            }
        }
        REG_ASSERT(all_valid, "All 20 tokens preserved in exact sequence after repeated reallocations");
        
        token_array_free(&arr);
        REG_ASSERT(arr.tokens == NULL && arr.count == 0, "Array cleanly freed after growth");
    }
    
    END_TEST_CASE();
}

/* ========================================================================= */
/* Main runner for regression suite                                          */
/* ========================================================================= */

#ifdef STANDALONE_TEST
int main(void) {
#else
int run_test_regression(void) {
#endif
    printf("======================================================================\n");
    printf("  PLSB LEXER - PERMANENT REGRESSION & HARDENING TEST SUITE\n");
    printf("======================================================================\n\n");
    
    test_dot_lookahead_reported_case();
    test_dot_sequences_across_buffer_sizes();
    test_operator_lookahead_boundaries();
    test_operator_non_matching_lookahead();
    test_eof_immediately_after_lookahead();
    test_number_literals_with_dots();
    test_long_tokens_multi_refill();
    test_injected_allocation_failures();
    test_arithmetic_overflow_guards();
    test_io_error_propagation_and_cleanup();
    test_token_array_allocation_safety();
    
    printf("\n----------------------------------------------------------------------\n");
    printf("Regression Suite Summary:\n");
    printf("  Test Cases:  %d passed, %d failed (Total %d cases)\n",
           g_cases_passed, g_cases_failed, g_cases_count);
    printf("  Assertions:  %d passed, %d failed (Total %d checks)\n",
           g_assert_passed, g_assert_failed, g_assert_count);
    printf("======================================================================\n");
    
    return (g_cases_failed == 0 && g_assert_failed == 0) ? 0 : 1;
}
