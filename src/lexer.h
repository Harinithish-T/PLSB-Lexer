#ifndef PLSB_LEXER_H
#define PLSB_LEXER_H

#include <stddef.h>
#include <stdbool.h>
#include "input_buffer.h"
#include "spill_buffer.h"
#include "predictor.h"
#include "statistics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_ERROR,
    
    /* Identifiers & Keywords */
    TOKEN_IDENTIFIER,
    TOKEN_KW_AUTO,
    TOKEN_KW_BREAK,
    TOKEN_KW_CASE,
    TOKEN_KW_CHAR,
    TOKEN_KW_CONST,
    TOKEN_KW_CONTINUE,
    TOKEN_KW_DEFAULT,
    TOKEN_KW_DO,
    TOKEN_KW_DOUBLE,
    TOKEN_KW_ELSE,
    TOKEN_KW_ENUM,
    TOKEN_KW_EXTERN,
    TOKEN_KW_FLOAT,
    TOKEN_KW_FOR,
    TOKEN_KW_GOTO,
    TOKEN_KW_IF,
    TOKEN_KW_INT,
    TOKEN_KW_LONG,
    TOKEN_KW_REGISTER,
    TOKEN_KW_RETURN,
    TOKEN_KW_SHORT,
    TOKEN_KW_SIGNED,
    TOKEN_KW_SIZEOF,
    TOKEN_KW_STATIC,
    TOKEN_KW_STRUCT,
    TOKEN_KW_SWITCH,
    TOKEN_KW_TYPEDEF,
    TOKEN_KW_UNION,
    TOKEN_KW_UNSIGNED,
    TOKEN_KW_VOID,
    TOKEN_KW_VOLATILE,
    TOKEN_KW_WHILE,
    TOKEN_KW_INLINE,
    TOKEN_KW_RESTRICT,
    TOKEN_KW_BOOL,
    TOKEN_KW_COMPLEX,
    TOKEN_KW_IMAGINARY,
    
    /* Literals */
    TOKEN_INT_LITERAL,
    TOKEN_FLOAT_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_CHAR_LITERAL,
    
    /* Comments */
    TOKEN_COMMENT,
    
    /* Operators */
    TOKEN_OP_PLUS,          /* + */
    TOKEN_OP_MINUS,         /* - */
    TOKEN_OP_STAR,          /* * */
    TOKEN_OP_SLASH,         /* / */
    TOKEN_OP_PERCENT,       /* % */
    TOKEN_OP_ASSIGN,        /* = */
    TOKEN_OP_PLUS_ASSIGN,   /* += */
    TOKEN_OP_MINUS_ASSIGN,  /* -= */
    TOKEN_OP_STAR_ASSIGN,   /* *= */
    TOKEN_OP_SLASH_ASSIGN,  /* /= */
    TOKEN_OP_PERCENT_ASSIGN,/* %= */
    TOKEN_OP_EQ,            /* == */
    TOKEN_OP_NE,            /* != */
    TOKEN_OP_LT,            /* < */
    TOKEN_OP_GT,            /* > */
    TOKEN_OP_LE,            /* <= */
    TOKEN_OP_GE,            /* >= */
    TOKEN_OP_LOGIC_AND,     /* && */
    TOKEN_OP_LOGIC_OR,      /* || */
    TOKEN_OP_LOGIC_NOT,     /* ! */
    TOKEN_OP_BIT_AND,       /* & */
    TOKEN_OP_BIT_OR,        /* | */
    TOKEN_OP_BIT_XOR,       /* ^ */
    TOKEN_OP_BIT_NOT,       /* ~ */
    TOKEN_OP_LSHIFT,        /* << */
    TOKEN_OP_RSHIFT,        /* >> */
    TOKEN_OP_LSHIFT_ASSIGN, /* <<= */
    TOKEN_OP_RSHIFT_ASSIGN, /* >>= */
    TOKEN_OP_BIT_AND_ASSIGN,/* &= */
    TOKEN_OP_BIT_OR_ASSIGN, /* |= */
    TOKEN_OP_BIT_XOR_ASSIGN,/* ^= */
    TOKEN_OP_INC,           /* ++ */
    TOKEN_OP_DEC,           /* -- */
    TOKEN_OP_ARROW,         /* -> */
    TOKEN_OP_QUESTION,      /* ? */
    TOKEN_OP_COLON,         /* : */
    TOKEN_OP_HASH,          /* # */
    TOKEN_OP_HASH_HASH,     /* ## */
    
    /* Punctuation */
    TOKEN_LPAREN,           /* ( */
    TOKEN_RPAREN,           /* ) */
    TOKEN_LBRACKET,         /* [ */
    TOKEN_RBRACKET,         /* ] */
    TOKEN_LBRACE,           /* { */
    TOKEN_RBRACE,           /* } */
    TOKEN_SEMICOLON,        /* ; */
    TOKEN_COMMA,            /* , */
    TOKEN_DOT,              /* . */
    TOKEN_ELLIPSIS          /* ... */
} token_type_t;

typedef struct {
    token_type_t type;
    token_class_t class_type;
    char *lexeme;          /* Dynamically allocated null-terminated string */
    size_t length;
    size_t line;           /* 1-based start line */
    size_t column;         /* 1-based start column */
    bool was_spilled;      /* True if token crossed an input-buffer boundary */
    const char *error_msg; /* Error description if type == TOKEN_ERROR */
} token_t;

const char *token_type_name(token_type_t type);
void token_free(token_t *tok);

typedef struct {
    bool emit_comments;    /* If true, comments are emitted as TOKEN_COMMENT */
} lexer_options_t;

typedef struct {
    input_buffer_t *ib;
    spill_buffer_t spill;
    predictor_t *predictor;
    statistics_t *stats;
    lexer_options_t options;
    
    size_t current_line;
    size_t current_col;
    size_t prev_col;
    
    size_t token_start_line;
    size_t token_start_col;
    
    bool inside_token;
    bool is_spilled;
    token_class_t candidate_class;
} lexer_t;

/**
 * Initialize lexer with input buffer, spill policy, predictor, and statistics.
 */
void lexer_init(lexer_t *l, input_buffer_t *ib, spill_policy_t policy, 
                size_t baseline_init_cap, predictor_t *predictor, 
                statistics_t *stats, const lexer_options_t *opts);

/**
 * Fetch the next token from the input stream.
 * Allocates tok->lexeme; caller must call token_free(tok).
 */
int lexer_next_token(lexer_t *l, token_t *tok);

/**
 * Clean up lexer resources.
 */
void lexer_free(lexer_t *l);

#ifdef __cplusplus
}
#endif

#endif /* PLSB_LEXER_H */
