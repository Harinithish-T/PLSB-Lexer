#ifndef TOKEN_ARRAY_H
#define TOKEN_ARRAY_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "lexer.h"

/* Dynamic array of tokens used by benchmark equivalence oracle and test harness */
typedef struct {
    token_t *tokens;
    size_t count;
    size_t capacity;
} token_array_t;

typedef void *(*token_array_realloc_fn)(void *ptr, size_t size);

static inline bool token_array_init(token_array_t *arr, size_t initial_cap) {
    if (!arr) return false;
    arr->tokens = NULL;
    arr->count = 0;
    arr->capacity = (initial_cap > 0) ? initial_cap : 256;
    if (arr->capacity > SIZE_MAX / sizeof(token_t)) {
        arr->capacity = 0;
        return false;
    }
    arr->tokens = (token_t *)malloc(arr->capacity * sizeof(token_t));
    if (!arr->tokens) {
        arr->capacity = 0;
        return false;
    }
    return true;
}

static inline bool token_array_push_ex(token_array_t *arr, const token_t *tok, token_array_realloc_fn realloc_fn) {
    if (!arr || !arr->tokens || !tok) return false;
    if (arr->count >= arr->capacity) {
        /* Check arithmetic overflow before doubling */
        if (arr->capacity > (SIZE_MAX / 2) / sizeof(token_t)) {
            return false;
        }
        size_t new_cap = arr->capacity * 2;
        token_t *new_tokens = realloc_fn ? (token_t *)realloc_fn(arr->tokens, new_cap * sizeof(token_t))
                                         : (token_t *)realloc(arr->tokens, new_cap * sizeof(token_t));
        if (!new_tokens) {
            /* Keep original arr->tokens intact to allow safe cleanup without memory leak */
            return false;
        }
        arr->tokens = new_tokens;
        arr->capacity = new_cap;
    }
    arr->tokens[arr->count++] = *tok;
    return true;
}

static inline bool token_array_push(token_array_t *arr, const token_t *tok) {
    return token_array_push_ex(arr, tok, NULL);
}

static inline void token_array_free(token_array_t *arr) {
    if (arr && arr->tokens) {
        for (size_t i = 0; i < arr->count; ++i) {
            token_free(&arr->tokens[i]);
        }
        free(arr->tokens);
        arr->tokens = NULL;
    }
    if (arr) {
        arr->count = 0;
        arr->capacity = 0;
    }
}

#endif /* TOKEN_ARRAY_H */
