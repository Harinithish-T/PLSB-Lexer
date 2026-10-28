#include "lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef struct {
    const char *word;
    token_type_t type;
} keyword_entry_t;

static const keyword_entry_t KEYWORDS[] = {
    {"_Bool",      TOKEN_KW_BOOL},
    {"_Complex",   TOKEN_KW_COMPLEX},
    {"_Imaginary", TOKEN_KW_IMAGINARY},
    {"auto",       TOKEN_KW_AUTO},
    {"break",      TOKEN_KW_BREAK},
    {"case",       TOKEN_KW_CASE},
    {"char",       TOKEN_KW_CHAR},
    {"const",      TOKEN_KW_CONST},
    {"continue",   TOKEN_KW_CONTINUE},
    {"default",    TOKEN_KW_DEFAULT},
    {"do",         TOKEN_KW_DO},
    {"double",     TOKEN_KW_DOUBLE},
    {"else",       TOKEN_KW_ELSE},
    {"enum",       TOKEN_KW_ENUM},
    {"extern",     TOKEN_KW_EXTERN},
    {"float",      TOKEN_KW_FLOAT},
    {"for",        TOKEN_KW_FOR},
    {"goto",       TOKEN_KW_GOTO},
    {"if",         TOKEN_KW_IF},
    {"inline",     TOKEN_KW_INLINE},
    {"int",        TOKEN_KW_INT},
    {"long",       TOKEN_KW_LONG},
    {"register",   TOKEN_KW_REGISTER},
    {"restrict",   TOKEN_KW_RESTRICT},
    {"return",     TOKEN_KW_RETURN},
    {"short",      TOKEN_KW_SHORT},
    {"signed",     TOKEN_KW_SIGNED},
    {"sizeof",     TOKEN_KW_SIZEOF},
    {"static",     TOKEN_KW_STATIC},
    {"struct",     TOKEN_KW_STRUCT},
    {"switch",     TOKEN_KW_SWITCH},
    {"typedef",    TOKEN_KW_TYPEDEF},
    {"union",      TOKEN_KW_UNION},
    {"unsigned",   TOKEN_KW_UNSIGNED},
    {"void",       TOKEN_KW_VOID},
    {"volatile",   TOKEN_KW_VOLATILE},
    {"while",      TOKEN_KW_WHILE}
};

static const size_t NUM_KEYWORDS = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

static int compare_keyword(const void *key, const void *elem) {
    const char *str = (const char *)key;
    const keyword_entry_t *entry = (const keyword_entry_t *)elem;
    return strcmp(str, entry->word);
}

static token_type_t check_keyword(const char *name) {
    if (!name) return TOKEN_IDENTIFIER;
    keyword_entry_t *found = (keyword_entry_t *)bsearch(
        name, KEYWORDS, NUM_KEYWORDS, sizeof(keyword_entry_t), compare_keyword
    );
    return found ? found->type : TOKEN_IDENTIFIER;
}

const char *token_type_name(token_type_t type) {
    switch (type) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_ERROR: return "ERROR";
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_KW_AUTO: return "KW_AUTO";
        case TOKEN_KW_BREAK: return "KW_BREAK";
        case TOKEN_KW_CASE: return "KW_CASE";
        case TOKEN_KW_CHAR: return "KW_CHAR";
        case TOKEN_KW_CONST: return "KW_CONST";
        case TOKEN_KW_CONTINUE: return "KW_CONTINUE";
        case TOKEN_KW_DEFAULT: return "KW_DEFAULT";
        case TOKEN_KW_DO: return "KW_DO";
        case TOKEN_KW_DOUBLE: return "KW_DOUBLE";
        case TOKEN_KW_ELSE: return "KW_ELSE";
        case TOKEN_KW_ENUM: return "KW_ENUM";
        case TOKEN_KW_EXTERN: return "KW_EXTERN";
        case TOKEN_KW_FLOAT: return "KW_FLOAT";
        case TOKEN_KW_FOR: return "KW_FOR";
        case TOKEN_KW_GOTO: return "KW_GOTO";
        case TOKEN_KW_IF: return "KW_IF";
        case TOKEN_KW_INT: return "KW_INT";
        case TOKEN_KW_LONG: return "KW_LONG";
        case TOKEN_KW_REGISTER: return "KW_REGISTER";
        case TOKEN_KW_RETURN: return "KW_RETURN";
        case TOKEN_KW_SHORT: return "KW_SHORT";
        case TOKEN_KW_SIGNED: return "KW_SIGNED";
        case TOKEN_KW_SIZEOF: return "KW_SIZEOF";
        case TOKEN_KW_STATIC: return "KW_STATIC";
        case TOKEN_KW_STRUCT: return "KW_STRUCT";
        case TOKEN_KW_SWITCH: return "KW_SWITCH";
        case TOKEN_KW_TYPEDEF: return "KW_TYPEDEF";
        case TOKEN_KW_UNION: return "KW_UNION";
        case TOKEN_KW_UNSIGNED: return "KW_UNSIGNED";
        case TOKEN_KW_VOID: return "KW_VOID";
        case TOKEN_KW_VOLATILE: return "KW_VOLATILE";
        case TOKEN_KW_WHILE: return "KW_WHILE";
        case TOKEN_KW_INLINE: return "KW_INLINE";
        case TOKEN_KW_RESTRICT: return "KW_RESTRICT";
        case TOKEN_KW_BOOL: return "KW_BOOL";
        case TOKEN_KW_COMPLEX: return "KW_COMPLEX";
        case TOKEN_KW_IMAGINARY: return "KW_IMAGINARY";
        case TOKEN_INT_LITERAL: return "INT_LITERAL";
        case TOKEN_FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TOKEN_STRING_LITERAL: return "STRING_LITERAL";
        case TOKEN_CHAR_LITERAL: return "CHAR_LITERAL";
        case TOKEN_COMMENT: return "COMMENT";
        case TOKEN_OP_PLUS: return "OP_PLUS";
        case TOKEN_OP_MINUS: return "OP_MINUS";
        case TOKEN_OP_STAR: return "OP_STAR";
        case TOKEN_OP_SLASH: return "OP_SLASH";
        case TOKEN_OP_PERCENT: return "OP_PERCENT";
        case TOKEN_OP_ASSIGN: return "OP_ASSIGN";
        case TOKEN_OP_PLUS_ASSIGN: return "OP_PLUS_ASSIGN";
        case TOKEN_OP_MINUS_ASSIGN: return "OP_MINUS_ASSIGN";
        case TOKEN_OP_STAR_ASSIGN: return "OP_STAR_ASSIGN";
        case TOKEN_OP_SLASH_ASSIGN: return "OP_SLASH_ASSIGN";
        case TOKEN_OP_PERCENT_ASSIGN: return "OP_PERCENT_ASSIGN";
        case TOKEN_OP_EQ: return "OP_EQ";
        case TOKEN_OP_NE: return "OP_NE";
        case TOKEN_OP_LT: return "OP_LT";
        case TOKEN_OP_GT: return "OP_GT";
        case TOKEN_OP_LE: return "OP_LE";
        case TOKEN_OP_GE: return "OP_GE";
        case TOKEN_OP_LOGIC_AND: return "OP_LOGIC_AND";
        case TOKEN_OP_LOGIC_OR: return "OP_LOGIC_OR";
        case TOKEN_OP_LOGIC_NOT: return "OP_LOGIC_NOT";
        case TOKEN_OP_BIT_AND: return "OP_BIT_AND";
        case TOKEN_OP_BIT_OR: return "OP_BIT_OR";
        case TOKEN_OP_BIT_XOR: return "OP_BIT_XOR";
        case TOKEN_OP_BIT_NOT: return "OP_BIT_NOT";
        case TOKEN_OP_LSHIFT: return "OP_LSHIFT";
        case TOKEN_OP_RSHIFT: return "OP_RSHIFT";
        case TOKEN_OP_LSHIFT_ASSIGN: return "OP_LSHIFT_ASSIGN";
        case TOKEN_OP_RSHIFT_ASSIGN: return "OP_RSHIFT_ASSIGN";
        case TOKEN_OP_BIT_AND_ASSIGN: return "OP_BIT_AND_ASSIGN";
        case TOKEN_OP_BIT_OR_ASSIGN: return "OP_BIT_OR_ASSIGN";
        case TOKEN_OP_BIT_XOR_ASSIGN: return "OP_BIT_XOR_ASSIGN";
        case TOKEN_OP_INC: return "OP_INC";
        case TOKEN_OP_DEC: return "OP_DEC";
        case TOKEN_OP_ARROW: return "OP_ARROW";
        case TOKEN_OP_QUESTION: return "OP_QUESTION";
        case TOKEN_OP_COLON: return "OP_COLON";
        case TOKEN_OP_HASH: return "OP_HASH";
        case TOKEN_OP_HASH_HASH: return "OP_HASH_HASH";
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DOT: return "DOT";
        case TOKEN_ELLIPSIS: return "ELLIPSIS";
        default: return "UNKNOWN";
    }
}

void token_free(token_t *tok) {
    if (!tok) return;
    if (tok->lexeme) {
        plsb_free_fn ffn = spill_buffer_get_free();
        ffn(tok->lexeme);
        tok->lexeme = NULL;
    }
    tok->length = 0;
}

void lexer_init(lexer_t *l, input_buffer_t *ib, spill_policy_t policy, 
                size_t baseline_init_cap, predictor_t *predictor, 
                statistics_t *stats, const lexer_options_t *opts) {
    if (!l) return;
    memset(l, 0, sizeof(*l));
    l->ib = ib;
    l->predictor = predictor;
    l->stats = stats;
    if (opts) {
        l->options = *opts;
    } else {
        l->options.emit_comments = true;
    }
    
    spill_buffer_init(&l->spill, policy, baseline_init_cap, predictor, stats);
    
    l->current_line = 1;
    l->current_col = 1;
    l->prev_col = 1;
    l->inside_token = false;
    l->is_spilled = false;
}

void lexer_free(lexer_t *l) {
    if (!l) return;
    spill_buffer_free(&l->spill);
}

/**
 * Non-destructive lookahead helper.
 * Returns the character at lookahead offset k (0-based) from current forward pointer.
 * Returns -1 on EOF, -2 on I/O read error.
 * Does NOT move forward pointer or modify token state.
 */
static int lexer_peek(lexer_t *l, size_t k) {
    input_buffer_t *ib = l->ib;
    if (!ib) return -1;
    if (ib->io_error) return -2;
    
    size_t end_active = ib->bytes_read[ib->active_buffer];
    size_t cur_pos = (size_t)(ib->forward - ib->buffers[ib->active_buffer]);
    size_t bytes_left = (cur_pos < end_active) ? (end_active - cur_pos) : 0;
    
    if (k < bytes_left) {
        return (unsigned char)ib->forward[k];
    }
    
    /* Character lies beyond active buffer */
    size_t offset_in_other = k - bytes_left;
    int other_buf = 1 - ib->active_buffer;
    
    if (ib->bytes_read[other_buf] == 0 && !ib->eof_reached && !ib->io_error) {
        input_buffer_refill(ib, other_buf);
    }
    
    if (ib->io_error) return -2;
    
    if (offset_in_other < ib->bytes_read[other_buf]) {
        return (unsigned char)ib->buffers[other_buf][offset_in_other];
    }
    
    return -1; /* EOF */
}

/**
 * Consumes and returns the current character, advancing positions and spilling
 * accumulated lexeme prefix if crossing a buffer boundary.
 * Returns character code (0..255), -1 on EOF, -2 on I/O error, -3 on memory allocation failure.
 */
static int lexer_consume(lexer_t *l) {
    input_buffer_t *ib = l->ib;
    if (!ib) return -1;
    
    while (1) {
        size_t end_active = ib->bytes_read[ib->active_buffer];
        size_t cur_pos = (size_t)(ib->forward - ib->buffers[ib->active_buffer]);
        
        if (cur_pos >= end_active) {
            /* Boundary reached */
            if (ib->io_error) return -2;
            if (ib->eof_reached && ib->bytes_read[1 - ib->active_buffer] == 0) {
                return -1;
            }
            
            /* Spilling prefix across boundary */
            if (l->inside_token) {
                size_t chunk_len = (size_t)(ib->forward - ib->lexeme_start);
                if (!l->is_spilled) {
                    l->is_spilled = true;
                    if (spill_buffer_begin(&l->spill, l->candidate_class, chunk_len) != 0) {
                        return -3; /* Allocation failure */
                    }
                }
                if (chunk_len > 0) {
                    if (spill_buffer_append(&l->spill, ib->lexeme_start, chunk_len) != 0) {
                        return -3; /* Allocation failure */
                    }
                }
            }
            
            /* Alternate buffer */
            int next_buf = 1 - ib->active_buffer;
            if (ib->bytes_read[next_buf] == 0 && !ib->eof_reached && !ib->io_error) {
                input_buffer_refill(ib, next_buf);
            }
            
            ib->bytes_read[ib->active_buffer] = 0; /* retired buffer now consumed */
            ib->active_buffer = next_buf;
            ib->forward = ib->buffers[next_buf];
            ib->lexeme_start = ib->forward;
            
            if (l->stats) {
                l->stats->input_refill_count = ib->refill_count;
                l->stats->total_source_bytes = ib->total_bytes_read;
            }
            
            if (ib->io_error) return -2;
            if (ib->bytes_read[next_buf] == 0) {
                return -1; /* End of file */
            }
        }
        
        char c = *ib->forward++;
        l->prev_col = l->current_col;
        if (c == '\n') {
            l->current_line++;
            l->current_col = 1;
        } else {
            l->current_col++;
        }
        return (unsigned char)c;
    }
}

static void finalize_mem_error(lexer_t *l, token_t *tok) {
    tok->type = TOKEN_ERROR;
    tok->class_type = TOKEN_CLASS_OTHER;
    tok->line = l->token_start_line;
    tok->column = l->token_start_col;
    tok->error_msg = "Memory allocation failure";
    tok->lexeme = NULL;
    tok->length = 0;
    tok->was_spilled = l->is_spilled;
    
    l->inside_token = false;
    l->is_spilled = false;
    spill_buffer_reset(&l->spill);
    if (l->ib) l->ib->lexeme_start = l->ib->forward;
}

static void finalize_token(lexer_t *l, token_t *tok, token_type_t type, token_class_t cls) {
    tok->type = type;
    tok->class_type = cls;
    tok->line = l->token_start_line;
    tok->column = l->token_start_col;
    tok->error_msg = NULL;
    tok->lexeme = NULL;
    tok->length = 0;
    tok->was_spilled = false;
    
    input_buffer_t *ib = l->ib;
    if (!ib) return;
    
    if (l->is_spilled) {
        size_t tail_len = (size_t)(ib->forward - ib->lexeme_start);
        if (tail_len > 0) {
            if (spill_buffer_append(&l->spill, ib->lexeme_start, tail_len) != 0) {
                finalize_mem_error(l, tok);
                return;
            }
        }
        tok->lexeme = spill_buffer_finish(&l->spill, &tok->length);
        if (!tok->lexeme) {
            finalize_mem_error(l, tok);
            return;
        }
        tok->was_spilled = true;
    } else {
        size_t len = (size_t)(ib->forward - ib->lexeme_start);
        if (len > SIZE_MAX - 1) {
            finalize_mem_error(l, tok);
            return;
        }
        plsb_malloc_fn mfn = spill_buffer_get_malloc();
        tok->lexeme = (char *)mfn(len + 1);
        if (!tok->lexeme) {
            finalize_mem_error(l, tok);
            return;
        }
        memcpy(tok->lexeme, ib->lexeme_start, len);
        tok->lexeme[len] = '\0';
        tok->length = len;
        tok->was_spilled = false;
    }
    
    if (l->predictor && type != TOKEN_ERROR) {
        predictor_update(l->predictor, cls, tok->length);
    }
    if (l->stats && type != TOKEN_ERROR) {
        l->stats->total_tokens++;
        l->stats->class_counts[cls]++;
    }
    
    l->inside_token = false;
    l->is_spilled = false;
    ib->lexeme_start = ib->forward;
}

static void finalize_error(lexer_t *l, token_t *tok, const char *msg) {
    tok->type = TOKEN_ERROR;
    tok->class_type = TOKEN_CLASS_OTHER;
    tok->line = l->token_start_line;
    tok->column = l->token_start_col;
    tok->error_msg = msg;
    tok->lexeme = NULL;
    tok->length = 0;
    tok->was_spilled = l->is_spilled;
    
    l->inside_token = false;
    l->is_spilled = false;
    spill_buffer_reset(&l->spill);
    if (l->ib) l->ib->lexeme_start = l->ib->forward;
}

int lexer_next_token(lexer_t *l, token_t *tok) {
    if (!l || !tok) return -1;
    memset(tok, 0, sizeof(*tok));
    
    input_buffer_t *ib = l->ib;
    if (!ib) return -1;
    
    while (1) {
        /* 1. Skip whitespace outside of tokens */
        int p = lexer_peek(l, 0);
        if (p == -1) {
            /* Normal EOF */
            tok->type = TOKEN_EOF;
            tok->class_type = TOKEN_CLASS_OTHER;
            tok->line = l->current_line;
            tok->column = l->current_col;
            plsb_malloc_fn mfn = spill_buffer_get_malloc();
            tok->lexeme = (char *)mfn(1);
            if (!tok->lexeme) {
                finalize_mem_error(l, tok);
                return 1;
            }
            tok->lexeme[0] = '\0';
            tok->length = 0;
            tok->was_spilled = false;
            return 0;
        }
        if (p == -2) {
            l->token_start_line = l->current_line;
            l->token_start_col = l->current_col;
            finalize_error(l, tok, "I/O read error");
            return 1;
        }
        
        if (p == ' ' || p == '\t' || p == '\v' || p == '\f' || p == '\n' || p == '\r') {
            int consumed = lexer_consume(l);
            if (consumed == -3) {
                finalize_mem_error(l, tok);
                return 1;
            }
            if (p == '\r' && lexer_peek(l, 0) == '\n') {
                lexer_consume(l);
            }
            ib->lexeme_start = ib->forward;
            continue;
        }
        
        /* 2. Start of a new token */
        l->token_start_line = l->current_line;
        l->token_start_col = l->current_col;
        l->inside_token = true;
        l->is_spilled = false;
        ib->lexeme_start = ib->forward;
        
        int c = lexer_consume(l);
        if (c == -3) {
            finalize_mem_error(l, tok);
            return 1;
        }
        if (c == -2) {
            finalize_error(l, tok, "I/O read error");
            return 1;
        }
        if (c == -1) {
            tok->type = TOKEN_EOF;
            tok->class_type = TOKEN_CLASS_OTHER;
            tok->line = l->current_line;
            tok->column = l->current_col;
            plsb_malloc_fn mfn = spill_buffer_get_malloc();
            tok->lexeme = (char *)mfn(1);
            if (!tok->lexeme) {
                finalize_mem_error(l, tok);
                return 1;
            }
            tok->lexeme[0] = '\0';
            tok->length = 0;
            return 0;
        }
        
        /* Comments or Slash operator */
        if (c == '/') {
            int next = lexer_peek(l, 0);
            if (next == -2) {
                finalize_error(l, tok, "I/O read error");
                return 1;
            }
            if (next == '/') {
                /* Line comment */
                lexer_consume(l);
                l->candidate_class = TOKEN_CLASS_COMMENT;
                while (1) {
                    int sc = lexer_peek(l, 0);
                    if (sc == -2) {
                        finalize_error(l, tok, "I/O read error");
                        return 1;
                    }
                    if (sc == -1 || sc == '\n') break;
                    if (sc == '\r') {
                        int rnext = lexer_peek(l, 1);
                        if (rnext == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (rnext == '\n') break;
                    }
                    if (lexer_consume(l) == -3) {
                        finalize_mem_error(l, tok);
                        return 1;
                    }
                }
                if (l->options.emit_comments) {
                    finalize_token(l, tok, TOKEN_COMMENT, TOKEN_CLASS_COMMENT);
                    return 1;
                } else {
                    l->inside_token = false;
                    l->is_spilled = false;
                    spill_buffer_reset(&l->spill);
                    ib->lexeme_start = ib->forward;
                    continue;
                }
            } else if (next == '*') {
                /* Block comment */
                lexer_consume(l);
                l->candidate_class = TOKEN_CLASS_COMMENT;
                while (1) {
                    int bc = lexer_consume(l);
                    if (bc == -3) {
                        finalize_mem_error(l, tok);
                        return 1;
                    }
                    if (bc == -2) {
                        finalize_error(l, tok, "I/O read error");
                        return 1;
                    }
                    if (bc == -1) {
                        finalize_error(l, tok, "Unterminated block comment");
                        return 1;
                    }
                    if (bc == '*') {
                        int p0 = lexer_peek(l, 0);
                        if (p0 == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (p0 == '/') {
                            lexer_consume(l);
                            break;
                        }
                    }
                }
                if (l->options.emit_comments) {
                    finalize_token(l, tok, TOKEN_COMMENT, TOKEN_CLASS_COMMENT);
                    return 1;
                } else {
                    l->inside_token = false;
                    l->is_spilled = false;
                    spill_buffer_reset(&l->spill);
                    ib->lexeme_start = ib->forward;
                    continue;
                }
            } else if (next == '=') {
                lexer_consume(l);
                l->candidate_class = TOKEN_CLASS_OPERATOR;
                finalize_token(l, tok, TOKEN_OP_SLASH_ASSIGN, TOKEN_CLASS_OPERATOR);
                return 1;
            } else {
                l->candidate_class = TOKEN_CLASS_OPERATOR;
                finalize_token(l, tok, TOKEN_OP_SLASH, TOKEN_CLASS_OPERATOR);
                return 1;
            }
        }
        
        /* Identifiers and Keywords */
        if (isalpha(c) || c == '_') {
            l->candidate_class = TOKEN_CLASS_IDENTIFIER;
            while (1) {
                int ic = lexer_peek(l, 0);
                if (ic == -2) {
                    finalize_error(l, tok, "I/O read error");
                    return 1;
                }
                if (ic == -1 || (!isalnum(ic) && ic != '_')) {
                    break;
                }
                if (lexer_consume(l) == -3) {
                    finalize_mem_error(l, tok);
                    return 1;
                }
            }
            finalize_token(l, tok, TOKEN_IDENTIFIER, TOKEN_CLASS_IDENTIFIER);
            token_type_t kw = check_keyword(tok->lexeme);
            if (kw != TOKEN_IDENTIFIER) {
                tok->type = kw;
            }
            return 1;
        }
        
        /* Numbers */
        if (isdigit(c) || (c == '.' && isdigit(lexer_peek(l, 0)))) {
            l->candidate_class = TOKEN_CLASS_NUMBER;
            bool is_float = (c == '.');
            
            if (c == '0') {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) {
                    finalize_error(l, tok, "I/O read error");
                    return 1;
                }
                if (p0 == 'x' || p0 == 'X') {
                    lexer_consume(l);
                    while (1) {
                        int hc = lexer_peek(l, 0);
                        if (hc == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (hc == -1) break;
                        if (!isxdigit(hc) && hc != '.') break;
                        if (hc == '.') {
                            if (is_float) break;
                            is_float = true;
                        }
                        if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                    }
                } else if (p0 == 'b' || p0 == 'B') {
                    lexer_consume(l);
                    while (1) {
                        int bc = lexer_peek(l, 0);
                        if (bc == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (bc != '0' && bc != '1') break;
                        if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                    }
                } else {
                    while (1) {
                        int oc = lexer_peek(l, 0);
                        if (oc == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (oc == -1) break;
                        if (oc == '.') {
                            if (is_float) break;
                            int p1 = lexer_peek(l, 1);
                            if (p1 == -2) {
                                finalize_error(l, tok, "I/O read error");
                                return 1;
                            }
                            if (p1 == '.') break; /* e.g. 0.. */
                            is_float = true;
                            if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                        } else if (isdigit(oc)) {
                            if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                        } else {
                            break;
                        }
                    }
                }
            } else {
                while (1) {
                    int dc = lexer_peek(l, 0);
                    if (dc == -2) {
                        finalize_error(l, tok, "I/O read error");
                        return 1;
                    }
                    if (dc == -1) break;
                    if (dc == '.') {
                        if (is_float) break;
                        int p1 = lexer_peek(l, 1);
                        if (p1 == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (p1 == '.') break; /* e.g. 1.. */
                        is_float = true;
                        if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                    } else if (dc == 'e' || dc == 'E') {
                        int e1 = lexer_peek(l, 1);
                        if (e1 == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        int e2 = lexer_peek(l, 2);
                        if (e2 == -2) {
                            finalize_error(l, tok, "I/O read error");
                            return 1;
                        }
                        if (isdigit(e1) || ((e1 == '+' || e1 == '-') && isdigit(e2))) {
                            is_float = true;
                            if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                            int sign = lexer_peek(l, 0);
                            if (sign == -2) {
                                finalize_error(l, tok, "I/O read error");
                                return 1;
                            }
                            if (sign == '+' || sign == '-') {
                                if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                            }
                        } else {
                            break;
                        }
                    } else if (isdigit(dc)) {
                        if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                    } else {
                        break;
                    }
                }
            }
            
            /* Trailing type suffixes */
            while (1) {
                int sc = lexer_peek(l, 0);
                if (sc == -2) {
                    finalize_error(l, tok, "I/O read error");
                    return 1;
                }
                if (sc == 'u' || sc == 'U' || sc == 'l' || sc == 'L' || sc == 'f' || sc == 'F') {
                    if (lexer_consume(l) == -3) { finalize_mem_error(l, tok); return 1; }
                } else {
                    break;
                }
            }
            
            finalize_token(l, tok, is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL, TOKEN_CLASS_NUMBER);
            return 1;
        }
        
        /* String Literals */
        if (c == '"') {
            l->candidate_class = TOKEN_CLASS_STRING;
            while (1) {
                int sc = lexer_consume(l);
                if (sc == -3) {
                    finalize_mem_error(l, tok);
                    return 1;
                }
                if (sc == -2) {
                    finalize_error(l, tok, "I/O read error");
                    return 1;
                }
                if (sc == -1) {
                    finalize_error(l, tok, "Unterminated string literal");
                    return 1;
                }
                if (sc == '\\') {
                    int esc = lexer_consume(l);
                    if (esc == -3) { finalize_mem_error(l, tok); return 1; }
                    if (esc == -1 || esc == -2) {
                        finalize_error(l, tok, "Unterminated string literal at escape");
                        return 1;
                    }
                    continue;
                }
                if (sc == '"') {
                    break;
                }
            }
            finalize_token(l, tok, TOKEN_STRING_LITERAL, TOKEN_CLASS_STRING);
            return 1;
        }
        
        /* Character Literals */
        if (c == '\'') {
            l->candidate_class = TOKEN_CLASS_CHAR;
            while (1) {
                int cc = lexer_consume(l);
                if (cc == -3) {
                    finalize_mem_error(l, tok);
                    return 1;
                }
                if (cc == -2) {
                    finalize_error(l, tok, "I/O read error");
                    return 1;
                }
                if (cc == -1) {
                    finalize_error(l, tok, "Unterminated character literal");
                    return 1;
                }
                if (cc == '\\') {
                    int esc = lexer_consume(l);
                    if (esc == -3) { finalize_mem_error(l, tok); return 1; }
                    if (esc == -1 || esc == -2) {
                        finalize_error(l, tok, "Unterminated character literal at escape");
                        return 1;
                    }
                    continue;
                }
                if (cc == '\'') {
                    break;
                }
            }
            finalize_token(l, tok, TOKEN_CHAR_LITERAL, TOKEN_CLASS_CHAR);
            return 1;
        }
        
        /* Multi-character Operators and Punctuation */
        l->candidate_class = TOKEN_CLASS_OPERATOR;
        switch (c) {
            case '+': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '+') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_INC, TOKEN_CLASS_OPERATOR); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_PLUS_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_PLUS, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '-': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '-') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_DEC, TOKEN_CLASS_OPERATOR); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_MINUS_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                if (p0 == '>') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_ARROW, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_MINUS, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '*': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_STAR_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_STAR, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '%': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_PERCENT_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_PERCENT, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '=': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_EQ, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_ASSIGN, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '!': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_NE, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_LOGIC_NOT, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '<': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') {
                    lexer_consume(l);
                    finalize_token(l, tok, TOKEN_OP_LE, TOKEN_CLASS_OPERATOR);
                    return 1;
                }
                if (p0 == '<') {
                    lexer_consume(l);
                    int p1 = lexer_peek(l, 0);
                    if (p1 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                    if (p1 == '=') {
                        lexer_consume(l);
                        finalize_token(l, tok, TOKEN_OP_LSHIFT_ASSIGN, TOKEN_CLASS_OPERATOR);
                        return 1;
                    }
                    finalize_token(l, tok, TOKEN_OP_LSHIFT, TOKEN_CLASS_OPERATOR);
                    return 1;
                }
                finalize_token(l, tok, TOKEN_OP_LT, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '>': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') {
                    lexer_consume(l);
                    finalize_token(l, tok, TOKEN_OP_GE, TOKEN_CLASS_OPERATOR);
                    return 1;
                }
                if (p0 == '>') {
                    lexer_consume(l);
                    int p1 = lexer_peek(l, 0);
                    if (p1 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                    if (p1 == '=') {
                        lexer_consume(l);
                        finalize_token(l, tok, TOKEN_OP_RSHIFT_ASSIGN, TOKEN_CLASS_OPERATOR);
                        return 1;
                    }
                    finalize_token(l, tok, TOKEN_OP_RSHIFT, TOKEN_CLASS_OPERATOR);
                    return 1;
                }
                finalize_token(l, tok, TOKEN_OP_GT, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '&': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '&') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_LOGIC_AND, TOKEN_CLASS_OPERATOR); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_BIT_AND_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_BIT_AND, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '|': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '|') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_LOGIC_OR, TOKEN_CLASS_OPERATOR); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_BIT_OR_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_BIT_OR, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '^': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '=') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_BIT_XOR_ASSIGN, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_BIT_XOR, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            case '~': finalize_token(l, tok, TOKEN_OP_BIT_NOT, TOKEN_CLASS_OPERATOR); return 1;
            case '?': finalize_token(l, tok, TOKEN_OP_QUESTION, TOKEN_CLASS_OPERATOR); return 1;
            case ':': finalize_token(l, tok, TOKEN_OP_COLON, TOKEN_CLASS_OPERATOR); return 1;
            case '#': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '#') { lexer_consume(l); finalize_token(l, tok, TOKEN_OP_HASH_HASH, TOKEN_CLASS_OPERATOR); return 1; }
                finalize_token(l, tok, TOKEN_OP_HASH, TOKEN_CLASS_OPERATOR);
                return 1;
            }
            
            /* Punctuation */
            case '(': finalize_token(l, tok, TOKEN_LPAREN, TOKEN_CLASS_PUNCTUATION); return 1;
            case ')': finalize_token(l, tok, TOKEN_RPAREN, TOKEN_CLASS_PUNCTUATION); return 1;
            case '[': finalize_token(l, tok, TOKEN_LBRACKET, TOKEN_CLASS_PUNCTUATION); return 1;
            case ']': finalize_token(l, tok, TOKEN_RBRACKET, TOKEN_CLASS_PUNCTUATION); return 1;
            case '{': finalize_token(l, tok, TOKEN_LBRACE, TOKEN_CLASS_PUNCTUATION); return 1;
            case '}': finalize_token(l, tok, TOKEN_RBRACE, TOKEN_CLASS_PUNCTUATION); return 1;
            case ';': finalize_token(l, tok, TOKEN_SEMICOLON, TOKEN_CLASS_PUNCTUATION); return 1;
            case ',': finalize_token(l, tok, TOKEN_COMMA, TOKEN_CLASS_PUNCTUATION); return 1;
            case '.': {
                int p0 = lexer_peek(l, 0);
                if (p0 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                int p1 = lexer_peek(l, 1);
                if (p1 == -2) { finalize_error(l, tok, "I/O read error"); return 1; }
                if (p0 == '.' && p1 == '.') {
                    lexer_consume(l);
                    lexer_consume(l);
                    finalize_token(l, tok, TOKEN_ELLIPSIS, TOKEN_CLASS_PUNCTUATION);
                    return 1;
                }
                finalize_token(l, tok, TOKEN_DOT, TOKEN_CLASS_PUNCTUATION);
                return 1;
            }
            default:
                finalize_error(l, tok, "Unexpected character");
                return 1;
        }
    }
}
