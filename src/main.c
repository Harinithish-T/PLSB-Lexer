#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "input_buffer.h"
#include "lexer.h"
#include "predictor.h"
#include "statistics.h"
#include "report.h"

#ifdef _WIN32
#include <windows.h>
static double get_time_sec(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#elif defined(CLOCK_MONOTONIC)
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#else
static double get_time_sec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}
#endif

typedef enum {
    MODE_BASELINE,
    MODE_PLSB,
    MODE_COMPARE
} run_mode_t;

static void print_usage(const char *prog) {
    printf("Predictive Lexeme Spill Buffering (PLSB) - Lexer Driver\n");
    printf("Usage: %s [options] <input_file>\n\n", prog);
    printf("Options:\n");
    printf("  --mode <baseline|plsb|compare>  Execution mode (default: compare)\n");
    printf("  --buffer-size <bytes>           Input buffer capacity (default: 4096)\n");
    printf("  --baseline-cap <bytes>          Baseline initial spill capacity (default: 16)\n");
    printf("  --rule <combined|p75|ewma|max|weighted|markov>  PLSB rule (default: combined)\n");
    printf("  --alpha <0.0 - 1.0>             EWMA smoothing alpha (default: 0.20)\n");
    printf("  --format <text|json|csv>        Output report format (default: text)\n");
    printf("  --dump-tokens                   Print every emitted token\n");
    printf("  --help                          Display this help message\n");
}

static int run_single_pass(const char *filepath, size_t buffer_size, spill_policy_t policy,
                           const predictor_config_t *pred_cfg, statistics_t *stats, 
                           bool dump_tokens, token_t **out_tokens, size_t *out_token_count) {
    input_buffer_t ib;
    if (input_buffer_init_file(&ib, filepath, buffer_size) != 0) {
        fprintf(stderr, "Error: Failed to open source file: %s\n", filepath);
        return -1;
    }
    
    predictor_t predictor;
    if (policy == SPILL_POLICY_PLSB) {
        predictor_init(&predictor, pred_cfg);
    } else {
        predictor_init_default(&predictor);
    }
    
    statistics_init(stats);
    
    lexer_options_t lopts = { .emit_comments = true };
    lexer_t lexer;
    lexer_init(&lexer, &ib, policy, pred_cfg ? pred_cfg->baseline_init_capacity : 16,
               (policy == SPILL_POLICY_PLSB) ? &predictor : NULL, stats, &lopts);
    
    token_t *tok_list = NULL;
    size_t tok_cap = 0;
    size_t tok_cnt = 0;
    
    clock_t start_cpu = clock();
    double start_wall = get_time_sec();
    
    token_t tok;
    while (1) {
        int status = lexer_next_token(&lexer, &tok);
        if (status <= 0 && tok.type == TOKEN_EOF) {
            token_free(&tok);
            break;
        }
        
        if (dump_tokens) {
            printf("[%zu:%zu] %-16s '%s'%s\n", 
                   tok.line, tok.column, token_type_name(tok.type), tok.lexeme,
                   tok.was_spilled ? " [SPILLED]" : "");
        }
        
        if (out_tokens) {
            if (tok_cnt >= tok_cap) {
                if (tok_cap > (SIZE_MAX / 2) / sizeof(token_t)) {
                    fprintf(stderr, "Error: Token array capacity overflow\n");
                    for (size_t i = 0; i < tok_cnt; ++i) token_free(&tok_list[i]);
                    free(tok_list);
                    token_free(&tok);
                    lexer_free(&lexer);
                    input_buffer_free(&ib);
                    return -1;
                }
                size_t new_cap = (tok_cap == 0) ? 128 : (tok_cap * 2);
                token_t *new_list = (token_t *)realloc(tok_list, new_cap * sizeof(token_t));
                if (!new_list) {
                    fprintf(stderr, "Error: Memory allocation failure while buffering tokens\n");
                    for (size_t i = 0; i < tok_cnt; ++i) token_free(&tok_list[i]);
                    free(tok_list);
                    token_free(&tok);
                    lexer_free(&lexer);
                    input_buffer_free(&ib);
                    return -1;
                }
                tok_list = new_list;
                tok_cap = new_cap;
            }
            tok_list[tok_cnt++] = tok;
        } else {
            token_free(&tok);
        }
    }
    
    double end_wall = get_time_sec();
    clock_t end_cpu = clock();
    
    double wall_sec = end_wall - start_wall;
    double cpu_sec = (double)(end_cpu - start_cpu) / (double)CLOCKS_PER_SEC;
    
    statistics_finalize(stats, (policy == SPILL_POLICY_PLSB) ? &predictor : NULL, wall_sec, cpu_sec);
    
    lexer_free(&lexer);
    input_buffer_free(&ib);
    
    if (out_tokens) {
        *out_tokens = tok_list;
        *out_token_count = tok_cnt;
    }
    return 0;
}

int main(int argc, char **argv) {
    run_mode_t mode = MODE_COMPARE;
    size_t buffer_size = 4096;
    const char *format = "text";
    bool dump_tokens = false;
    const char *filepath = NULL;
    
    predictor_config_t pcfg;
    pcfg.rule = PRED_RULE_COMBINED;
    pcfg.ewma_alpha = 0.20;
    pcfg.weighted_p75_weight = 0.70;
    pcfg.safe_min_capacity = 16;
    pcfg.max_capacity = 4096;
    pcfg.min_samples_for_p75 = 5;
    pcfg.baseline_init_capacity = 16;
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "baseline") == 0) mode = MODE_BASELINE;
            else if (strcmp(argv[i], "plsb") == 0) mode = MODE_PLSB;
            else if (strcmp(argv[i], "compare") == 0) mode = MODE_COMPARE;
            else { fprintf(stderr, "Unknown mode: %s\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            buffer_size = (size_t)strtoul(argv[++i], NULL, 10);
            if (buffer_size == 0) buffer_size = 1;
        } else if (strcmp(argv[i], "--baseline-cap") == 0 && i + 1 < argc) {
            pcfg.baseline_init_capacity = (size_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--rule") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "baseline") == 0) pcfg.rule = PRED_RULE_BASELINE;
            else if (strcmp(argv[i], "p75") == 0) pcfg.rule = PRED_RULE_P75_ONLY;
            else if (strcmp(argv[i], "ewma") == 0) pcfg.rule = PRED_RULE_EWMA_ONLY;
            else if (strcmp(argv[i], "max") == 0) pcfg.rule = PRED_RULE_MAX_P75_EWMA;
            else if (strcmp(argv[i], "weighted") == 0) pcfg.rule = PRED_RULE_WEIGHTED;
            else if (strcmp(argv[i], "markov") == 0) pcfg.rule = PRED_RULE_MARKOV_CONDITIONED;
            else if (strcmp(argv[i], "combined") == 0 || strcmp(argv[i], "plsb") == 0)
                pcfg.rule = PRED_RULE_COMBINED;
            else { fprintf(stderr, "Unknown rule: %s\n", argv[i]); return 1; }
        } else if (strcmp(argv[i], "--alpha") == 0 && i + 1 < argc) {
            pcfg.ewma_alpha = atof(argv[++i]);
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (strcmp(argv[i], "--dump-tokens") == 0) {
            dump_tokens = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            filepath = argv[i];
        } else {
            fprintf(stderr, "Unknown flag: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (!filepath) {
        fprintf(stderr, "Error: No input file specified.\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (mode == MODE_BASELINE) {
        statistics_t stats;
        if (run_single_pass(filepath, buffer_size, SPILL_POLICY_BASELINE, &pcfg, &stats, dump_tokens, NULL, NULL) != 0) {
            return 1;
        }
        if (strcmp(format, "json") == 0) report_write_json(stdout, &stats, "Baseline");
        else if (strcmp(format, "csv") == 0) { report_write_csv_header(stdout); report_write_csv_row(stdout, &stats, "Baseline", filepath, buffer_size); }
        else report_print_summary(stdout, &stats, "Conventional Baseline");
    } else if (mode == MODE_PLSB) {
        statistics_t stats;
        if (run_single_pass(filepath, buffer_size, SPILL_POLICY_PLSB, &pcfg, &stats, dump_tokens, NULL, NULL) != 0) {
            return 1;
        }
        if (strcmp(format, "json") == 0) report_write_json(stdout, &stats, "PLSB");
        else if (strcmp(format, "csv") == 0) { report_write_csv_header(stdout); report_write_csv_row(stdout, &stats, "PLSB", filepath, buffer_size); }
        else report_print_summary(stdout, &stats, "Predictive Lexeme Spill Buffering (PLSB)");
    } else {
        /* Comparative evaluation: run both, verify exact identical token stream */
        statistics_t base_stats, plsb_stats;
        token_t *base_tokens = NULL; size_t base_count = 0;
        token_t *plsb_tokens = NULL; size_t plsb_count = 0;
        
        if (run_single_pass(filepath, buffer_size, SPILL_POLICY_BASELINE, &pcfg, &base_stats, false, &base_tokens, &base_count) != 0) return 1;
        if (run_single_pass(filepath, buffer_size, SPILL_POLICY_PLSB, &pcfg, &plsb_stats, false, &plsb_tokens, &plsb_count) != 0) {
            for (size_t i = 0; i < base_count; ++i) token_free(&base_tokens[i]);
            free(base_tokens);
            return 1;
        }
        
        /* Strict Oracle Equivalence Verification */
        bool match = true;
        if (base_count != plsb_count) {
            fprintf(stderr, "EQUIVALENCE FAILURE: Token counts differ! Baseline: %zu, PLSB: %zu\n", base_count, plsb_count);
            match = false;
        } else {
            for (size_t i = 0; i < base_count; ++i) {
                const char *b_lex = base_tokens[i].lexeme ? base_tokens[i].lexeme : "";
                const char *p_lex = plsb_tokens[i].lexeme ? plsb_tokens[i].lexeme : "";
                if (base_tokens[i].type != plsb_tokens[i].type ||
                    base_tokens[i].line != plsb_tokens[i].line ||
                    base_tokens[i].column != plsb_tokens[i].column ||
                    strcmp(b_lex, p_lex) != 0) {
                    fprintf(stderr, "EQUIVALENCE FAILURE at token %zu!\n", i);
                    fprintf(stderr, "  Baseline: [%zu:%zu] type %s '%s'\n",
                            base_tokens[i].line, base_tokens[i].column,
                            token_type_name(base_tokens[i].type), b_lex);
                    fprintf(stderr, "  PLSB:     [%zu:%zu] type %s '%s'\n",
                            plsb_tokens[i].line, plsb_tokens[i].column,
                            token_type_name(plsb_tokens[i].type), p_lex);
                    match = false;
                    break;
                }
            }
        }
        
        for (size_t i = 0; i < base_count; ++i) token_free(&base_tokens[i]);
        for (size_t i = 0; i < plsb_count; ++i) token_free(&plsb_tokens[i]);
        free(base_tokens);
        free(plsb_tokens);
        
        if (!match) {
            fprintf(stderr, "ABORT: Lexer outputs did not match identically across policies!\n");
            return 2;
        }
        
        if (strcmp(format, "json") == 0) {
            report_write_json(stdout, &base_stats, "Baseline");
            report_write_json(stdout, &plsb_stats, "PLSB");
        } else if (strcmp(format, "csv") == 0) {
            report_write_csv_header(stdout);
            report_write_csv_row(stdout, &base_stats, "Baseline", filepath, buffer_size);
            report_write_csv_row(stdout, &plsb_stats, "PLSB", filepath, buffer_size);
        } else {
            printf("\n[OK] Oracle Equivalence Confirmed: Baseline and PLSB generated 100%% identical tokens (%zu tokens).\n\n", base_count);
            report_print_comparison(stdout, &base_stats, &plsb_stats);
        }
    }
    return 0;
}
