#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/input_buffer.h"
#include "../../src/lexer.h"

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

static void test_buffer_sizes_matrix(void) {
    const char *code = "int x = 42; // comment\nchar *s = \"hello world!\";\nreturn x + 1;";
    size_t capacities[] = {1, 2, 3, 7, 16, 31, 32, 64, 256, 1024};
    size_t num_caps = sizeof(capacities) / sizeof(capacities[0]);
    
    /* First scan with large buffer 4096 to get oracle tokens */
    input_buffer_t ib_ref;
    input_buffer_init_string(&ib_ref, code, strlen(code), 4096);
    lexer_t l_ref;
    lexer_init(&l_ref, &ib_ref, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t ref_tokens[100];
    size_t ref_count = 0;
    while (1) {
        token_t t;
        lexer_next_token(&l_ref, &t);
        if (t.type == TOKEN_EOF) {
            token_free(&t);
            break;
        }
        ref_tokens[ref_count++] = t;
    }
    lexer_free(&l_ref);
    input_buffer_free(&ib_ref);
    
    /* Now test every buffer size down to 1 byte! */
    for (size_t c = 0; c < num_caps; ++c) {
        size_t cap = capacities[c];
        input_buffer_t ib;
        input_buffer_init_string(&ib, code, strlen(code), cap);
        lexer_t l;
        lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
        
        size_t idx = 0;
        bool match = true;
        while (1) {
            token_t t;
            lexer_next_token(&l, &t);
            if (t.type == TOKEN_EOF) {
                token_free(&t);
                break;
            }
            if (idx >= ref_count || 
                t.type != ref_tokens[idx].type || 
                strcmp(t.lexeme, ref_tokens[idx].lexeme) != 0 ||
                t.line != ref_tokens[idx].line ||
                t.column != ref_tokens[idx].column) {
                match = false;
                token_free(&t);
                break;
            }
            token_free(&t);
            idx++;
        }
        ASSERT_TEST(match && (idx == ref_count), "Buffer capacity matrix check matched oracle");
        lexer_free(&l);
        input_buffer_free(&ib);
    }
    
    for (size_t i = 0; i < ref_count; ++i) {
        token_free(&ref_tokens[i]);
    }
}

static void test_boundary_crossing_identifier(void) {
    /* Test identifier ending before, at, and after boundary */
    /* With buffer size 4:
       Buffer 0: "abcd"
       Buffer 1: "efgh"
       "abcdefgh" starts at 0, spans boundary at byte 4! */
    const char *src = "abcdefgh";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 4);
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t tok;
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_IDENTIFIER, "Identifier recognized across boundary");
    ASSERT_TEST(strcmp(tok.lexeme, "abcdefgh") == 0, "Spilled lexeme matches 'abcdefgh'");
    ASSERT_TEST(tok.was_spilled == true, "Token marked as spilled");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_multi_buffer_spanning_token(void) {
    /* 1500-character identifier spanning across 16-byte buffers */
    char long_id[1501];
    memset(long_id, 'a', 1500);
    long_id[1500] = '\0';
    
    input_buffer_t ib;
    input_buffer_init_string(&ib, long_id, 1500, 16);
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t tok;
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_IDENTIFIER, "1500-char identifier parsed");
    ASSERT_TEST(tok.length == 1500, "Length is exactly 1500");
    ASSERT_TEST(strcmp(tok.lexeme, long_id) == 0, "All 1500 chars preserved without loss");
    ASSERT_TEST(tok.was_spilled == true, "Token was spilled across multiple buffers");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_operator_split_across_boundary(void) {
    /* With buffer size 1: "==" has first '=' in buffer 0, second '=' in buffer 1 */
    const char *src = "==";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, 2, 1);
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t tok;
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_OP_EQ, "== operator recognized when split across 1-byte buffers");
    ASSERT_TEST(strcmp(tok.lexeme, "==") == 0, "Lexeme is ==");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_comment_delimiter_split_across_boundary(void) {
    /* Buffer size 2: slash-star split across buffer boundary */
    const char *src = "/* comment */";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 2);
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t tok;
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_COMMENT, "Comment parsed across 2-byte buffers");
    ASSERT_TEST(strcmp(tok.lexeme, "/* comment */") == 0, "Comment lexeme matches");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_crlf_and_no_newline(void) {
    const char *src = "foo\r\nbar";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 4);
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t tok;
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_IDENTIFIER && strcmp(tok.lexeme, "foo") == 0, "First token foo");
    ASSERT_TEST(tok.line == 1, "foo is on line 1");
    token_free(&tok);
    
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_IDENTIFIER && strcmp(tok.lexeme, "bar") == 0, "Second token bar");
    ASSERT_TEST(tok.line == 2, "bar is on line 2 (CRLF handled)");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

#ifdef STANDALONE_TEST
int main(void) {
#else
int run_test_boundary_conditions(void) {
#endif
    printf("[TEST] Running Integration Tests: Boundary Conditions & Buffer Refills...\n");
    test_buffer_sizes_matrix();
    test_boundary_crossing_identifier();
    test_multi_buffer_spanning_token();
    test_operator_split_across_boundary();
    test_comment_delimiter_split_across_boundary();
    test_crlf_and_no_newline();
    
    printf("  Results: %d/%d tests passed (%d failures)\n\n", 
           g_tests_passed, g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
