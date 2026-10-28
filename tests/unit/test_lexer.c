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

static void test_keywords_and_identifiers(void) {
    const char *src = "int count = 100;\nif (count > 0) return _Bool;";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 64);
    
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    
    token_t tok;
    
    /* 1. int */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_KW_INT, "Token 'int' recognized as keyword");
    ASSERT_TEST(strcmp(tok.lexeme, "int") == 0, "Lexeme matches");
    ASSERT_TEST(tok.line == 1 && tok.column == 1, "Line 1, Col 1");
    token_free(&tok);
    
    /* 2. count */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_IDENTIFIER, "Token 'count' is identifier");
    ASSERT_TEST(strcmp(tok.lexeme, "count") == 0, "Lexeme is 'count'");
    token_free(&tok);
    
    /* 3. = */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_OP_ASSIGN, "Token '=' is assign operator");
    token_free(&tok);
    
    /* 4. 100 */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_INT_LITERAL, "Token '100' is integer");
    token_free(&tok);
    
    /* 5. ; */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_SEMICOLON, "Token ';' is semicolon");
    token_free(&tok);
    
    /* 6. if */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_KW_IF, "Token 'if' is keyword");
    ASSERT_TEST(tok.line == 2 && tok.column == 1, "Line 2, Col 1");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_string_escapes_and_chars(void) {
    const char *src = "\"hello\\n\\\"world\\\"\" 'x' '\\'' '\\\\' ";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 128);
    
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    token_t tok;
    
    /* String literal */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_STRING_LITERAL, "String literal recognized");
    ASSERT_TEST(strcmp(tok.lexeme, "\"hello\\n\\\"world\\\"\"") == 0, "String preserves escapes");
    token_free(&tok);
    
    /* 'x' */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_CHAR_LITERAL, "Char 'x' recognized");
    ASSERT_TEST(strcmp(tok.lexeme, "'x'") == 0, "Lexeme is 'x'");
    token_free(&tok);
    
    /* '\'' */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_CHAR_LITERAL, "Escaped char '\\'' recognized");
    token_free(&tok);
    
    /* '\\' */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_CHAR_LITERAL, "Escaped backslash char recognized");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_comments_and_operators(void) {
    const char *src = "/* block */ // line\n++ -- <<= ->";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 128);
    
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    token_t tok;
    
    /* Block comment */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_COMMENT, "Block comment emitted");
    ASSERT_TEST(strcmp(tok.lexeme, "/* block */") == 0, "Block comment content");
    token_free(&tok);
    
    /* Line comment */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_COMMENT, "Line comment emitted");
    token_free(&tok);
    
    /* ++ */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_OP_INC, "Operator ++");
    token_free(&tok);
    
    /* -- */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_OP_DEC, "Operator --");
    token_free(&tok);
    
    /* <<= */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_OP_LSHIFT_ASSIGN, "Operator <<=");
    token_free(&tok);
    
    /* -> */
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_OP_ARROW, "Operator ->");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

static void test_error_tokens(void) {
    const char *src = "\"unterminated string";
    input_buffer_t ib;
    input_buffer_init_string(&ib, src, strlen(src), 64);
    
    lexer_t l;
    lexer_init(&l, &ib, SPILL_POLICY_BASELINE, 16, NULL, NULL, NULL);
    token_t tok;
    
    lexer_next_token(&l, &tok);
    ASSERT_TEST(tok.type == TOKEN_ERROR, "Unterminated string emits TOKEN_ERROR");
    ASSERT_TEST(tok.error_msg != NULL, "Error message provided");
    token_free(&tok);
    
    lexer_free(&l);
    input_buffer_free(&ib);
}

#ifdef STANDALONE_TEST
int main(void) {
#else
int run_test_lexer(void) {
#endif
    printf("[TEST] Running Unit Tests: Lexer Rules & C Syntax...\n");
    test_keywords_and_identifiers();
    test_string_escapes_and_chars();
    test_comments_and_operators();
    test_error_tokens();
    
    printf("  Results: %d/%d tests passed (%d failures)\n\n", 
           g_tests_passed, g_tests_run, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
