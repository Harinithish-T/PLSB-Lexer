#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include "../src/input_buffer.h"
#include "../src/lexer.h"
#include "../src/predictor.h"
#include "../src/statistics.h"
#include "../src/report.h"
#include "../src/token_array.h"

#if defined(__clang__)
#define BENCH_COMPILER "Clang"
#elif defined(__GNUC__)
#define BENCH_COMPILER "GCC"
#elif defined(_MSC_VER)
#define BENCH_COMPILER "MSVC"
#else
#define BENCH_COMPILER "C11"
#endif

#define BENCH_CFLAGS "-std=c11 -Wall -Wextra -Wpedantic -O2 -Isrc"

#ifdef _WIN32
#include <windows.h>
static double bench_time_sec(void) {
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
        return -1.0;
    }
    if (freq.QuadPart <= 0) return -1.0;
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
static double bench_time_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif

/* ========================================================================= */
/* Benchmark Policy Definitions                                              */
/* ========================================================================= */

typedef struct {
    const char *name;
    const char *short_id;
    spill_policy_t policy;
    prediction_rule_t rule;
    size_t baseline_init_cap;
} bench_policy_cfg_t;

static const bench_policy_cfg_t POLICIES[] = {
    {"Baseline (16B init)",       "base16",  SPILL_POLICY_BASELINE, PRED_RULE_BASELINE,           16},
    {"Baseline (32B init)",       "base32",  SPILL_POLICY_BASELINE, PRED_RULE_BASELINE,           32},
    {"Baseline (64B init)",       "base64",  SPILL_POLICY_BASELINE, PRED_RULE_BASELINE,           64},
    {"PLSB (P75-Only)",           "p75",     SPILL_POLICY_PLSB,     PRED_RULE_P75_ONLY,           16},
    {"PLSB (EWMA-Only)",          "ewma",    SPILL_POLICY_PLSB,     PRED_RULE_EWMA_ONLY,          16},
    {"PLSB (Max P75+EWMA)",       "max",     SPILL_POLICY_PLSB,     PRED_RULE_MAX_P75_EWMA,       16},
    {"PLSB (Weighted 70/30)",     "weight",  SPILL_POLICY_PLSB,     PRED_RULE_WEIGHTED,           16},
    {"PLSB (Markov-Conditioned)", "markov",  SPILL_POLICY_PLSB,     PRED_RULE_MARKOV_CONDITIONED, 16}
};

static const size_t NUM_POLICIES = sizeof(POLICIES) / sizeof(POLICIES[0]);

/* Standard buffer size matrix */
static const size_t FULL_BUFFER_SIZES[] = {16, 32, 64, 256, 1024, 4096, 16384};
static const size_t NUM_FULL_BUFFER_SIZES = sizeof(FULL_BUFFER_SIZES) / sizeof(FULL_BUFFER_SIZES[0]);

static const size_t QUICK_BUFFER_SIZES[] = {32, 256, 4096};
static const size_t NUM_QUICK_BUFFER_SIZES = sizeof(QUICK_BUFFER_SIZES) / sizeof(QUICK_BUFFER_SIZES[0]);

/* Registered datasets */
static const char *const DEFAULT_DATASETS[] = {
    "tests/fixtures/sample_c.c",
    "tests/fixtures/stress_long_tokens.c",
    "tests/fixtures/long_identifiers.c",
    "tests/fixtures/long_comments.c",
    "tests/fixtures/long_strings.c",
    "benchmarks/datasets/realworld_c_sample.c"
};
static const size_t NUM_DEFAULT_DATASETS = sizeof(DEFAULT_DATASETS) / sizeof(DEFAULT_DATASETS[0]);

/* ========================================================================= */
/* Statistics Tracking Structures                                            */
/* ========================================================================= */

typedef struct {
    int iteration;
    bool success;
    const char *status_str;
    double wall_time_sec;
    uint64_t tokens;
    uint64_t source_bytes;
    uint64_t spill_events;
    uint64_t reallocations;
    uint64_t copy_bytes;
    size_t peak_capacity;
    uint64_t over_res_bytes;
    uint64_t refills;
    uint64_t hits;
    uint64_t misses;
    double tokens_per_sec;
    double mb_per_sec;
} bench_observation_t;

typedef struct {
    double mean;
    double median;
    double stddev;
    double min;
    double max;
} metric_stats_t;

typedef struct {
    const char *experiment_id;
    const char *dataset;
    const bench_policy_cfg_t *policy_cfg;
    size_t buffer_size;
    int planned_iterations;
    int successful_iterations;
    int failed_iterations;
    bool success;
    const char *correctness_status;
    
    metric_stats_t wall_time_ms;
    metric_stats_t throughput_ktok_s;
    metric_stats_t throughput_mb_s;
    metric_stats_t spill_events;
    metric_stats_t reallocations;
    metric_stats_t copy_bytes;
    metric_stats_t peak_capacity;
    metric_stats_t over_res_bytes;
    metric_stats_t refills;
    metric_stats_t hits;
    metric_stats_t misses;
    
    uint64_t total_tokens;
    uint64_t total_source_bytes;
} bench_summary_t;

/* ========================================================================= */
/* Math Helpers for Sample Statistics                                        */
/* ========================================================================= */

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static metric_stats_t compute_metric_stats(const double *vals, int count) {
    metric_stats_t res = {0.0, 0.0, 0.0, 0.0, 0.0};
    if (count <= 0) return res;

    double sum = 0.0;
    double min_v = vals[0];
    double max_v = vals[0];

    for (int i = 0; i < count; ++i) {
        sum += vals[i];
        if (vals[i] < min_v) min_v = vals[i];
        if (vals[i] > max_v) max_v = vals[i];
    }
    res.mean = sum / (double)count;
    res.min = min_v;
    res.max = max_v;

    /* Sample variance and standard deviation */
    if (count > 1) {
        double sq_diff_sum = 0.0;
        for (int i = 0; i < count; ++i) {
            double diff = vals[i] - res.mean;
            sq_diff_sum += (diff * diff);
        }
        res.stddev = sqrt(sq_diff_sum / (double)(count - 1));
    } else {
        res.stddev = 0.0;
    }

    /* Median */
    double *sorted = (double *)malloc(count * sizeof(double));
    if (sorted) {
        memcpy(sorted, vals, count * sizeof(double));
        qsort(sorted, count, sizeof(double), compare_doubles);
        if (count % 2 == 1) {
            res.median = sorted[count / 2];
        } else {
            res.median = (sorted[(count / 2) - 1] + sorted[count / 2]) / 2.0;
        }
        free(sorted);
    } else {
        res.median = res.mean;
    }

    return res;
}

/* ========================================================================= */
/* Token Array Storage Helper with Strict Allocation & Overflow Checks      */
/* ========================================================================= */

/* token_array_t and helpers (token_array_init, token_array_push, token_array_free)
   are declared in token_array.h */

static inline bool lexemes_equal(const char *a, const char *b) {
    if (a == NULL && b == NULL) return true;
    if (a == NULL || b == NULL) return false;
    return strcmp(a, b) == 0;
}

/* ========================================================================= */
/* Equivalence Oracle (Outside Timed Region)                                 */
/* ========================================================================= */

static bool verify_policy_equivalence(const char *filepath, size_t buffer_size,
                                     const bench_policy_cfg_t *pcfg,
                                     uint64_t *out_expected_tokens, uint64_t *out_expected_bytes) {
    if (out_expected_tokens) *out_expected_tokens = 0;
    if (out_expected_bytes) *out_expected_bytes = 0;

    /* 1. Collect reference tokens using Baseline policy */
    input_buffer_t ib_ref;
    if (input_buffer_init_file(&ib_ref, filepath, buffer_size) != 0) {
        fprintf(stderr, "  [ORACLE ERROR] Unable to open '%s' for reference run\n", filepath);
        return false;
    }
    lexer_options_t lopts = { .emit_comments = true };
    statistics_t ref_stats;
    statistics_init(&ref_stats);
    lexer_t lex_ref;
    lexer_init(&lex_ref, &ib_ref, SPILL_POLICY_BASELINE, 16, NULL, &ref_stats, &lopts);

    token_array_t ref_tokens;
    if (!token_array_init(&ref_tokens, 256)) {
        fprintf(stderr, "  [ORACLE ERROR] Memory allocation failed initializing reference token array\n");
        lexer_free(&lex_ref);
        input_buffer_free(&ib_ref);
        return false;
    }

    bool ref_success = true;
    while (1) {
        token_t tok;
        int status = lexer_next_token(&lex_ref, &tok);
        if (status < 0 || tok.type == TOKEN_ERROR) {
            fprintf(stderr, "  [ORACLE ERROR] Reference baseline run produced error token on token %zu\n", ref_tokens.count);
            token_free(&tok);
            ref_success = false;
            break;
        }
        if (tok.type == TOKEN_EOF) {
            token_free(&tok);
            break;
        }
        if (!token_array_push(&ref_tokens, &tok)) {
            fprintf(stderr, "  [ORACLE ERROR] Memory allocation failed while buffering reference token %zu\n", ref_tokens.count);
            token_free(&tok);
            ref_success = false;
            break;
        }
    }

    uint64_t ref_source_bytes = ref_stats.total_source_bytes;
    lexer_free(&lex_ref);
    input_buffer_free(&ib_ref);

    if (!ref_success) {
        token_array_free(&ref_tokens);
        return false;
    }

    if (out_expected_tokens) *out_expected_tokens = ref_tokens.count;
    if (out_expected_bytes) *out_expected_bytes = ref_source_bytes;

    /* 2. Run candidate policy and verify equivalence token by token */
    input_buffer_t ib_cand;
    if (input_buffer_init_file(&ib_cand, filepath, buffer_size) != 0) {
        token_array_free(&ref_tokens);
        return false;
    }

    predictor_config_t cfg;
    cfg.rule = pcfg->rule;
    cfg.ewma_alpha = 0.20;
    cfg.weighted_p75_weight = 0.70;
    cfg.safe_min_capacity = 16;
    cfg.max_capacity = 4096;
    cfg.min_samples_for_p75 = 5;
    cfg.baseline_init_capacity = pcfg->baseline_init_cap;

    predictor_t predictor;
    if (pcfg->policy == SPILL_POLICY_PLSB) {
        predictor_init(&predictor, &cfg);
    } else {
        predictor_init_default(&predictor);
    }

    statistics_t cand_stats;
    statistics_init(&cand_stats);

    lexer_t lex_cand;
    lexer_init(&lex_cand, &ib_cand, pcfg->policy, pcfg->baseline_init_cap,
               (pcfg->policy == SPILL_POLICY_PLSB) ? &predictor : NULL, &cand_stats, &lopts);

    size_t cand_index = 0;
    bool match = true;

    while (1) {
        token_t tok;
        int status = lexer_next_token(&lex_cand, &tok);
        if (status < 0 || tok.type == TOKEN_ERROR) {
            fprintf(stderr, "  [ORACLE MISMATCH] Policy '%s' returned lexer error on token index %zu (%s)\n",
                    pcfg->name, cand_index, tok.error_msg ? tok.error_msg : "Unknown error");
            token_free(&tok);
            match = false;
            break;
        }
        if (tok.type == TOKEN_EOF) {
            token_free(&tok);
            break;
        }

        if (cand_index >= ref_tokens.count) {
            fprintf(stderr, "  [ORACLE MISMATCH] Policy '%s' emitted extra token '%s' (index %zu)\n",
                    pcfg->name, tok.lexeme ? tok.lexeme : "(null)", cand_index);
            token_free(&tok);
            match = false;
            break;
        }

        const token_t *ref_tok = &ref_tokens.tokens[cand_index];
        if (tok.type != ref_tok->type ||
            tok.line != ref_tok->line ||
            tok.column != ref_tok->column ||
            tok.length != ref_tok->length ||
            !lexemes_equal(tok.lexeme, ref_tok->lexeme)) {
            fprintf(stderr, "  [ORACLE MISMATCH] Difference at token %zu:\n"
                            "    Expected: Type=%d Line=%zu Col=%zu Len=%zu Lexeme='%s'\n"
                            "    Actual:   Type=%d Line=%zu Col=%zu Len=%zu Lexeme='%s'\n",
                    cand_index,
                    ref_tok->type, ref_tok->line, ref_tok->column, ref_tok->length, ref_tok->lexeme ? ref_tok->lexeme : "(null)",
                    tok.type, tok.line, tok.column, tok.length, tok.lexeme ? tok.lexeme : "(null)");
            token_free(&tok);
            match = false;
            break;
        }

        token_free(&tok);
        cand_index++;
    }

    if (match && cand_index != ref_tokens.count) {
        fprintf(stderr, "  [ORACLE MISMATCH] Token count mismatch: expected %zu, got %zu\n",
                ref_tokens.count, cand_index);
        match = false;
    }

    lexer_free(&lex_cand);
    input_buffer_free(&ib_cand);
    token_array_free(&ref_tokens);

    return match;
}

/* ========================================================================= */
/* Benchmark Execution for a Single Configuration Matrix Cell                */
/* ========================================================================= */

static bool run_single_policy_benchmark(const char *experiment_id,
                                       const char *filepath, size_t buffer_size,
                                       const bench_policy_cfg_t *pcfg, int iterations,
                                       bench_observation_t *obs_out, bench_summary_t *summary_out) {
    memset(summary_out, 0, sizeof(*summary_out));
    summary_out->experiment_id = experiment_id;
    summary_out->dataset = filepath;
    summary_out->policy_cfg = pcfg;
    summary_out->buffer_size = buffer_size;
    summary_out->planned_iterations = iterations;
    summary_out->successful_iterations = 0;
    summary_out->failed_iterations = 0;
    summary_out->success = false;
    summary_out->correctness_status = "NOT_EVALUATED";

    uint64_t expected_tokens = 0;
    uint64_t expected_bytes = 0;

    /* Initialize all observation slots to failure/not run default state */
    for (int i = 0; i < iterations; ++i) {
        memset(&obs_out[i], 0, sizeof(obs_out[i]));
        obs_out[i].iteration = i + 1;
        obs_out[i].success = false;
        obs_out[i].status_str = "NOT_RUN";
    }

    /* 1. Equivalence oracle check outside timed loop */
    if (!verify_policy_equivalence(filepath, buffer_size, pcfg, &expected_tokens, &expected_bytes)) {
        fprintf(stderr, "  [ABORT] Equivalence verification failed for %s on %s (B=%zu)\n",
                pcfg->name, filepath, buffer_size);
        summary_out->correctness_status = "ORACLE_MISMATCH";
        summary_out->failed_iterations = iterations;
        for (int i = 0; i < iterations; ++i) {
            obs_out[i].status_str = "ORACLE_MISMATCH";
        }
        return false;
    }
    summary_out->correctness_status = "VERIFIED_EQUIVALENT";
    summary_out->total_tokens = expected_tokens;
    summary_out->total_source_bytes = expected_bytes;

    predictor_config_t cfg;
    cfg.rule = pcfg->rule;
    cfg.ewma_alpha = 0.20;
    cfg.weighted_p75_weight = 0.70;
    cfg.safe_min_capacity = 16;
    cfg.max_capacity = 4096;
    cfg.min_samples_for_p75 = 5;
    cfg.baseline_init_capacity = pcfg->baseline_init_cap;

    /* Allocate observation metric arrays */
    double *wall_times = (double *)malloc(iterations * sizeof(double));
    double *throughput_ktok = (double *)malloc(iterations * sizeof(double));
    double *throughput_mb = (double *)malloc(iterations * sizeof(double));
    double *spill_events = (double *)malloc(iterations * sizeof(double));
    double *reallocs = (double *)malloc(iterations * sizeof(double));
    double *copy_bytes = (double *)malloc(iterations * sizeof(double));
    double *peak_caps = (double *)malloc(iterations * sizeof(double));
    double *over_res = (double *)malloc(iterations * sizeof(double));
    double *refills = (double *)malloc(iterations * sizeof(double));
    double *hits = (double *)malloc(iterations * sizeof(double));
    double *misses = (double *)malloc(iterations * sizeof(double));

    if (!wall_times || !throughput_ktok || !throughput_mb || !spill_events ||
        !reallocs || !copy_bytes || !peak_caps || !over_res || !refills ||
        !hits || !misses) {
        fprintf(stderr, "  [ERROR] Memory allocation failed allocating metric arrays for %s\n", pcfg->name);
        free(wall_times); free(throughput_ktok); free(throughput_mb);
        free(spill_events); free(reallocs); free(copy_bytes);
        free(peak_caps); free(over_res); free(refills); free(hits); free(misses);
        summary_out->correctness_status = "ALLOCATION_FAILURE";
        summary_out->failed_iterations = iterations;
        for (int i = 0; i < iterations; ++i) {
            obs_out[i].status_str = "ALLOCATION_FAILURE";
        }
        return false;
    }

    int valid_count = 0;
    bool config_failed = false;

    for (int iter = 0; iter < iterations; ++iter) {
        bench_observation_t *ob = &obs_out[iter];
        memset(ob, 0, sizeof(*ob));
        ob->iteration = iter + 1;
        ob->success = false;
        ob->status_str = "PENDING";

        input_buffer_t ib;
        if (input_buffer_init_file(&ib, filepath, buffer_size) != 0) {
            fprintf(stderr, "  [ITERATION ERROR] Failed to open benchmark file: %s (iter %d)\n", filepath, iter + 1);
            ob->status_str = "FILE_OPEN_FAILED";
            summary_out->failed_iterations++;
            config_failed = true;
            break;
        }

        predictor_t predictor;
        if (pcfg->policy == SPILL_POLICY_PLSB) {
            predictor_init(&predictor, &cfg);
        } else {
            predictor_init_default(&predictor);
        }

        statistics_t stats;
        statistics_init(&stats);

        lexer_options_t lopts = { .emit_comments = true };
        lexer_t lexer;
        lexer_init(&lexer, &ib, pcfg->policy, pcfg->baseline_init_cap,
                   (pcfg->policy == SPILL_POLICY_PLSB) ? &predictor : NULL, &stats, &lopts);

        /* Timed Region: Token scanning only */
        double t0 = bench_time_sec();
        if (t0 < 0.0) {
            fprintf(stderr, "  [ITERATION ERROR] Timer failure reading t0 (iter %d)\n", iter + 1);
            ob->status_str = "TIMER_START_FAILED";
            lexer_free(&lexer);
            input_buffer_free(&ib);
            summary_out->failed_iterations++;
            config_failed = true;
            break;
        }

        uint64_t iter_tokens = 0;
        bool iter_error = false;

        while (1) {
            token_t tok;
            int status = lexer_next_token(&lexer, &tok);
            if (status < 0 || tok.type == TOKEN_ERROR) {
                token_free(&tok);
                iter_error = true;
                break;
            }
            if (tok.type == TOKEN_EOF) {
                token_free(&tok);
                break;
            }
            iter_tokens++;
            token_free(&tok);
        }

        double t1 = bench_time_sec();
        if (t1 < 0.0 || t1 < t0) {
            fprintf(stderr, "  [ITERATION ERROR] Timer failure reading t1 (iter %d)\n", iter + 1);
            iter_error = true;
            ob->status_str = "TIMER_END_FAILED";
        }

        double elapsed_sec = t1 - t0;
        if (elapsed_sec <= 0.0) {
            elapsed_sec = 1e-9; /* Safeguard minimum duration */
        }

        statistics_finalize(&stats, (pcfg->policy == SPILL_POLICY_PLSB) ? &predictor : NULL,
                            elapsed_sec, elapsed_sec);

        /* Verify token count integrity against reference */
        if (!iter_error && iter_tokens != expected_tokens) {
            fprintf(stderr, "  [ITERATION ERROR] Token count mismatch during timed run: expected %" PRIu64 ", got %" PRIu64 "\n",
                    expected_tokens, iter_tokens);
            iter_error = true;
            ob->status_str = "TOKEN_COUNT_MISMATCH";
        }

        /* Verify byte count integrity against reference */
        if (!iter_error && stats.total_source_bytes != expected_bytes) {
            fprintf(stderr, "  [ITERATION ERROR] Source byte mismatch during timed run: expected %" PRIu64 ", got %" PRIu64 "\n",
                    expected_bytes, stats.total_source_bytes);
            iter_error = true;
            ob->status_str = "SOURCE_BYTE_MISMATCH";
        }

        lexer_free(&lexer);
        input_buffer_free(&ib);

        if (iter_error) {
            if (strcmp(ob->status_str, "PENDING") == 0) {
                ob->status_str = "LEXER_TOKEN_ERROR";
            }
            summary_out->failed_iterations++;
            config_failed = true;
            break;
        }

        /* Record valid iteration observation */
        ob->success = true;
        ob->status_str = "SUCCESS";
        ob->wall_time_sec = elapsed_sec;
        ob->tokens = stats.total_tokens;
        ob->source_bytes = stats.total_source_bytes;
        ob->spill_events = stats.lexeme_spill_events;
        ob->reallocations = stats.spill_reallocations;
        ob->copy_bytes = stats.growth_copy_bytes;
        ob->peak_capacity = stats.peak_spill_capacity;
        ob->over_res_bytes = stats.over_reservation_bytes;
        ob->refills = stats.input_refill_count;
        ob->hits = stats.prediction_hits;
        ob->misses = stats.prediction_misses;
        ob->tokens_per_sec = (double)stats.total_tokens / elapsed_sec;
        ob->mb_per_sec = ((double)stats.total_source_bytes / elapsed_sec) / (1024.0 * 1024.0);

        wall_times[valid_count] = elapsed_sec * 1000.0; /* ms */
        throughput_ktok[valid_count] = ob->tokens_per_sec / 1000.0;
        throughput_mb[valid_count] = ob->mb_per_sec;
        spill_events[valid_count] = (double)ob->spill_events;
        reallocs[valid_count] = (double)ob->reallocations;
        copy_bytes[valid_count] = (double)ob->copy_bytes;
        peak_caps[valid_count] = (double)ob->peak_capacity;
        over_res[valid_count] = (double)ob->over_res_bytes;
        refills[valid_count] = (double)ob->refills;
        hits[valid_count] = (double)ob->hits;
        misses[valid_count] = (double)ob->misses;

        valid_count++;
        summary_out->successful_iterations++;
    }

    if (config_failed || valid_count < iterations) {
        summary_out->success = false;
        summary_out->correctness_status = "ITERATION_FAILED";
        free(wall_times); free(throughput_ktok); free(throughput_mb);
        free(spill_events); free(reallocs); free(copy_bytes);
        free(peak_caps); free(over_res); free(refills); free(hits); free(misses);
        return false;
    }

    /* Compute multi-run statistics exclusively over successful runs */
    summary_out->success = true;
    summary_out->wall_time_ms = compute_metric_stats(wall_times, valid_count);
    summary_out->throughput_ktok_s = compute_metric_stats(throughput_ktok, valid_count);
    summary_out->throughput_mb_s = compute_metric_stats(throughput_mb, valid_count);
    summary_out->spill_events = compute_metric_stats(spill_events, valid_count);
    summary_out->reallocations = compute_metric_stats(reallocs, valid_count);
    summary_out->copy_bytes = compute_metric_stats(copy_bytes, valid_count);
    summary_out->peak_capacity = compute_metric_stats(peak_caps, valid_count);
    summary_out->over_res_bytes = compute_metric_stats(over_res, valid_count);
    summary_out->refills = compute_metric_stats(refills, valid_count);
    summary_out->hits = compute_metric_stats(hits, valid_count);
    summary_out->misses = compute_metric_stats(misses, valid_count);

    free(wall_times); free(throughput_ktok); free(throughput_mb);
    free(spill_events); free(reallocs); free(copy_bytes);
    free(peak_caps); free(over_res); free(refills); free(hits); free(misses);

    return true;
}

/* ========================================================================= */
/* Reporting & File Output Functions                                         */
/* ========================================================================= */

static void print_terminal_table_header(const char *dataset, size_t buffer_size, int iterations) {
    printf("===============================================================================================================\n");
    printf(" BENCHMARK: %-32s | Buffer Size: %5zu B | Iterations: %2d\n", dataset, buffer_size, iterations);
    printf("===============================================================================================================\n");
    printf(" %-26s %-7s %-14s %-16s %-9s %-16s %-12s\n",
           "Policy", "Spills", "Reallocs (±σ)", "CopyBytes (±σ)", "PeakCap", "WallTime ms (±σ)", "Throughput");
    printf(" ---------------------------------------------------------------------------------------------------------------\n");
}

static void print_terminal_table_row(const bench_summary_t *s) {
    char realloc_str[32];
    char copy_str[32];
    char time_str[32];
    char tp_str[32];

    snprintf(realloc_str, sizeof(realloc_str), "%.1f ± %.1f", s->reallocations.mean, s->reallocations.stddev);
    snprintf(copy_str, sizeof(copy_str), "%.0f ± %.0f", s->copy_bytes.mean, s->copy_bytes.stddev);
    snprintf(time_str, sizeof(time_str), "%.3f ± %.3f", s->wall_time_ms.mean, s->wall_time_ms.stddev);
    snprintf(tp_str, sizeof(tp_str), "%.1f kTok/s", s->throughput_ktok_s.mean);

    printf(" %-26s %-7.0f %-14s %-16s %-9.0f %-16s %-12s\n",
           s->policy_cfg->name,
           s->spill_events.mean,
           realloc_str,
           copy_str,
           s->peak_capacity.mean,
           time_str,
           tp_str);
}

static void append_raw_csv(const char *csv_path, const char *experiment_id, const char *dataset,
                          size_t buffer_size, const bench_policy_cfg_t *pcfg,
                          const bench_observation_t *obs, int count) {
    bool file_exists = false;
    FILE *chk = fopen(csv_path, "r");
    if (chk) { file_exists = true; fclose(chk); }

    FILE *f = fopen(csv_path, "a");
    if (!f) return;

    if (!file_exists) {
        fprintf(f, "experiment_id,compiler,cflags,dataset,source_bytes,buffer_size,policy,policy_id,init_cap,iteration,"
                   "status,wall_time_sec,tokens,spill_events,reallocations,copy_bytes,peak_capacity,over_res_bytes,"
                   "refills,hits,misses,tokens_per_sec,mb_per_sec\n");
    }

    for (int i = 0; i < count; ++i) {
        const bench_observation_t *ob = &obs[i];
        fprintf(f, "\"%s\",\"%s\",\"%s\",\"%s\",%" PRIu64 ",%zu,\"%s\",\"%s\",%zu,%d,\"%s\","
                   "%.9f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu,%" PRIu64 ","
                   "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.2f,%.4f\n",
                experiment_id, BENCH_COMPILER, BENCH_CFLAGS, dataset, ob->source_bytes, buffer_size,
                pcfg->name, pcfg->short_id, pcfg->baseline_init_cap, ob->iteration,
                ob->status_str ? ob->status_str : (ob->success ? "SUCCESS" : "FAILED"),
                ob->wall_time_sec, ob->tokens, ob->spill_events, ob->reallocations,
                ob->copy_bytes, ob->peak_capacity, ob->over_res_bytes, ob->refills,
                ob->hits, ob->misses, ob->tokens_per_sec, ob->mb_per_sec);
    }
    fclose(f);
}

static void append_summary_csv(const char *csv_path, const bench_summary_t *s) {
    bool file_exists = false;
    FILE *chk = fopen(csv_path, "r");
    if (chk) { file_exists = true; fclose(chk); }

    FILE *f = fopen(csv_path, "a");
    if (!f) return;

    if (!file_exists) {
        fprintf(f, "experiment_id,compiler,cflags,dataset,source_bytes,buffer_size,policy,policy_id,init_cap,"
                   "planned_iterations,successful_iterations,failed_iterations,correctness_status,"
                   "wall_ms_mean,wall_ms_std,wall_ms_median,wall_ms_min,wall_ms_max,"
                   "ktok_mean,ktok_std,mb_mean,mb_std,"
                   "spill_events_mean,reallocs_mean,reallocs_std,reallocs_median,"
                   "copy_bytes_mean,copy_bytes_std,copy_bytes_median,"
                   "peak_cap_mean,over_res_mean,refills_mean,hits_mean,misses_mean\n");
    }

    fprintf(f, "\"%s\",\"%s\",\"%s\",\"%s\",%" PRIu64 ",%zu,\"%s\",\"%s\",%zu,%d,%d,%d,\"%s\","
               "%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.4f,%.4f,"
               "%.1f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
            s->experiment_id, BENCH_COMPILER, BENCH_CFLAGS, s->dataset, s->total_source_bytes, s->buffer_size,
            s->policy_cfg->name, s->policy_cfg->short_id, s->policy_cfg->baseline_init_cap,
            s->planned_iterations, s->successful_iterations, s->failed_iterations,
            s->correctness_status ? s->correctness_status : (s->success ? "VERIFIED_EQUIVALENT" : "FAILED"),
            s->wall_time_ms.mean, s->wall_time_ms.stddev, s->wall_time_ms.median, s->wall_time_ms.min, s->wall_time_ms.max,
            s->throughput_ktok_s.mean, s->throughput_ktok_s.stddev, s->throughput_mb_s.mean, s->throughput_mb_s.stddev,
            s->spill_events.mean, s->reallocations.mean, s->reallocations.stddev, s->reallocations.median,
            s->copy_bytes.mean, s->copy_bytes.stddev, s->copy_bytes.median,
            s->peak_capacity.mean, s->over_res_bytes.mean, s->refills.mean, s->hits.mean, s->misses.mean);
    fclose(f);
}

/* ========================================================================= */
/* JSON Export                                                               */
/* ========================================================================= */

static bool write_full_json_report(const char *json_path, const char *experiment_id,
                                   const char *matrix_mode, const bench_summary_t *summaries,
                                   size_t summary_count) {
    FILE *f = fopen(json_path, "w");
    if (!f) {
        fprintf(stderr, "Failed to open '%s' for JSON export\n", json_path);
        return false;
    }

    time_t now = time(NULL);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    fprintf(f, "{\n");
    fprintf(f, "  \"generator\": \"PLSB Benchmark Runner v2.1\",\n");
    fprintf(f, "  \"experiment_id\": \"%s\",\n", experiment_id);
    fprintf(f, "  \"matrix_mode\": \"%s\",\n", matrix_mode);
    fprintf(f, "  \"compiler\": \"%s\",\n", BENCH_COMPILER);
    fprintf(f, "  \"cflags\": \"%s\",\n", BENCH_CFLAGS);
    fprintf(f, "  \"timestamp\": \"%s\",\n", time_buf);
    fprintf(f, "  \"summary_count\": %zu,\n", summary_count);
    fprintf(f, "  \"results\": [\n");

    for (size_t i = 0; i < summary_count; ++i) {
        const bench_summary_t *s = &summaries[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"experiment_id\": \"%s\",\n", s->experiment_id);
        fprintf(f, "      \"dataset\": \"%s\",\n", s->dataset);
        fprintf(f, "      \"buffer_size_bytes\": %zu,\n", s->buffer_size);
        fprintf(f, "      \"policy_name\": \"%s\",\n", s->policy_cfg->name);
        fprintf(f, "      \"policy_id\": \"%s\",\n", s->policy_cfg->short_id);
        fprintf(f, "      \"initial_capacity\": %zu,\n", s->policy_cfg->baseline_init_cap);
        fprintf(f, "      \"planned_iterations\": %d,\n", s->planned_iterations);
        fprintf(f, "      \"successful_iterations\": %d,\n", s->successful_iterations);
        fprintf(f, "      \"failed_iterations\": %d,\n", s->failed_iterations);
        fprintf(f, "      \"success\": %s,\n", s->success ? "true" : "false");
        fprintf(f, "      \"correctness_status\": \"%s\",\n", s->correctness_status ? s->correctness_status : "UNKNOWN");
        fprintf(f, "      \"tokens\": %" PRIu64 ",\n", s->total_tokens);
        fprintf(f, "      \"source_bytes\": %" PRIu64 ",\n", s->total_source_bytes);
        
        fprintf(f, "      \"wall_time_ms\": {\"mean\": %.4f, \"stddev\": %.4f, \"median\": %.4f, \"min\": %.4f, \"max\": %.4f},\n",
                s->wall_time_ms.mean, s->wall_time_ms.stddev, s->wall_time_ms.median, s->wall_time_ms.min, s->wall_time_ms.max);
        fprintf(f, "      \"throughput_ktok_s\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->throughput_ktok_s.mean, s->throughput_ktok_s.stddev, s->throughput_ktok_s.median, s->throughput_ktok_s.min, s->throughput_ktok_s.max);
        fprintf(f, "      \"throughput_mb_s\": {\"mean\": %.4f, \"stddev\": %.4f, \"median\": %.4f, \"min\": %.4f, \"max\": %.4f},\n",
                s->throughput_mb_s.mean, s->throughput_mb_s.stddev, s->throughput_mb_s.median, s->throughput_mb_s.min, s->throughput_mb_s.max);
        
        fprintf(f, "      \"spill_events\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->spill_events.mean, s->spill_events.stddev, s->spill_events.median, s->spill_events.min, s->spill_events.max);
        fprintf(f, "      \"reallocations\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->reallocations.mean, s->reallocations.stddev, s->reallocations.median, s->reallocations.min, s->reallocations.max);
        fprintf(f, "      \"copy_bytes\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->copy_bytes.mean, s->copy_bytes.stddev, s->copy_bytes.median, s->copy_bytes.min, s->copy_bytes.max);
        fprintf(f, "      \"peak_capacity\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->peak_capacity.mean, s->peak_capacity.stddev, s->peak_capacity.median, s->peak_capacity.min, s->peak_capacity.max);
        fprintf(f, "      \"over_reservation_bytes\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->over_res_bytes.mean, s->over_res_bytes.stddev, s->over_res_bytes.median, s->over_res_bytes.min, s->over_res_bytes.max);
        fprintf(f, "      \"refills\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->refills.mean, s->refills.stddev, s->refills.median, s->refills.min, s->refills.max);
        fprintf(f, "      \"prediction_hits\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f},\n",
                s->hits.mean, s->hits.stddev, s->hits.median, s->hits.min, s->hits.max);
        fprintf(f, "      \"prediction_misses\": {\"mean\": %.2f, \"stddev\": %.2f, \"median\": %.2f, \"min\": %.2f, \"max\": %.2f}\n",
                s->misses.mean, s->misses.stddev, s->misses.median, s->misses.min, s->misses.max);
        
        fprintf(f, "    }%s\n", (i + 1 < summary_count) ? "," : "");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    bool write_ok = (ferror(f) == 0);
    if (fclose(f) != 0) write_ok = false;
    return write_ok;
}

/* ========================================================================= */
/* Command-Line Argument Processing & Main Dispatcher                       */
/* ========================================================================= */

static void print_usage(const char *prog) {
    printf("Predictive Lexeme Spill Buffering (PLSB) - Scientific Benchmark Engine v2.1\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --dataset <path>        Evaluate specific dataset file\n");
    printf("  --all-datasets          Evaluate all registered default datasets\n");
    printf("  --buffer-size <bytes>   Evaluate specific buffer size (e.g. 16, 32, 64, 256, 1024, 4096, 16384)\n");
    printf("  --iterations <N>        Iterations per cell (default: 5)\n");
    printf("  --quick                 Quick mode: 3 buffer sizes (32, 256, 4096), 3 iterations\n");
    printf("  --full                  Full mode: 7 buffer sizes (16..16384), 10 iterations\n");
    printf("  --overwrite             Overwrite output CSV files (default behavior)\n");
    printf("  --append                Append to existing output CSV files\n");
    printf("  --json <path>           Custom path for JSON output (default: results/benchmark_latest.json)\n");
    printf("  --raw-csv <path>        Custom path for raw observations CSV\n");
    printf("  --summary-csv <path>    Custom path for summary metrics CSV\n");
    printf("  --help                  Display this help message\n");
}

int run_benchmark_suite(int argc, char **argv) {
    const char *custom_dataset = NULL;
    bool all_datasets = false;
    size_t single_buffer_size = 0;
    int iterations = 5;
    bool iterations_specified = false;
    bool quick_mode = false;
    bool full_mode = false;
    bool append_csv = false;
    const char *custom_json = "results/benchmark_latest.json";
    const char *custom_raw_csv = "results/benchmark_raw.csv";
    const char *custom_summary_csv = "results/benchmark_summary.csv";

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dataset") == 0 && i + 1 < argc) {
            custom_dataset = argv[++i];
        } else if (strcmp(argv[i], "--all-datasets") == 0) {
            all_datasets = true;
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            single_buffer_size = (size_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
            if (iterations < 1) iterations = 1;
            iterations_specified = true;
        } else if (strcmp(argv[i], "--quick") == 0) {
            quick_mode = true;
            if (!iterations_specified) iterations = 3;
        } else if (strcmp(argv[i], "--full") == 0) {
            full_mode = true;
            if (!iterations_specified) iterations = 10;
        } else if (strcmp(argv[i], "--append") == 0) {
            append_csv = true;
        } else if (strcmp(argv[i], "--overwrite") == 0) {
            append_csv = false;
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            custom_json = argv[++i];
        } else if (strcmp(argv[i], "--raw-csv") == 0 && i + 1 < argc) {
            custom_raw_csv = argv[++i];
        } else if (strcmp(argv[i], "--summary-csv") == 0 && i + 1 < argc) {
            custom_summary_csv = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Dataset selection */
    const char **datasets = NULL;
    size_t dataset_count = 0;

    if (custom_dataset) {
        datasets = (const char **)malloc(sizeof(char *));
        if (!datasets) { fprintf(stderr, "Fatal memory allocation failure\n"); return 1; }
        datasets[0] = custom_dataset;
        dataset_count = 1;
    } else if (all_datasets || full_mode) {
        datasets = (const char **)malloc(NUM_DEFAULT_DATASETS * sizeof(char *));
        if (!datasets) { fprintf(stderr, "Fatal memory allocation failure\n"); return 1; }
        for (size_t i = 0; i < NUM_DEFAULT_DATASETS; ++i) {
            datasets[i] = DEFAULT_DATASETS[i];
        }
        dataset_count = NUM_DEFAULT_DATASETS;
    } else {
        /* Default: stress_long_tokens.c */
        datasets = (const char **)malloc(sizeof(char *));
        if (!datasets) { fprintf(stderr, "Fatal memory allocation failure\n"); return 1; }
        datasets[0] = "tests/fixtures/stress_long_tokens.c";
        dataset_count = 1;
    }

    /* Buffer sizes selection */
    const size_t *buffer_sizes = NULL;
    size_t buffer_size_count = 0;

    if (single_buffer_size > 0) {
        size_t *sb = (size_t *)malloc(sizeof(size_t));
        if (!sb) { fprintf(stderr, "Fatal memory allocation failure\n"); free(datasets); return 1; }
        sb[0] = single_buffer_size;
        buffer_sizes = sb;
        buffer_size_count = 1;
    } else if (quick_mode) {
        buffer_sizes = QUICK_BUFFER_SIZES;
        buffer_size_count = NUM_QUICK_BUFFER_SIZES;
    } else if (full_mode) {
        buffer_sizes = FULL_BUFFER_SIZES;
        buffer_size_count = NUM_FULL_BUFFER_SIZES;
    } else {
        buffer_sizes = QUICK_BUFFER_SIZES;
        buffer_size_count = NUM_QUICK_BUFFER_SIZES;
    }

    /* Generate unique experiment identifier */
    time_t raw_time = time(NULL);
    char exp_id[64];
    strftime(exp_id, sizeof(exp_id), "exp_%Y%m%d_%H%M%S", localtime(&raw_time));
    const char *matrix_mode = full_mode ? "full" : (quick_mode ? "quick" : "custom");

    printf("\n===============================================================================================================\n");
    printf("  PLSB EXPERIMENTAL BENCHMARK ENGINE — REPRODUCIBLE RESEARCH SUITE v2.1\n");
    printf("===============================================================================================================\n");
    printf("  Experiment ID:   %s\n", exp_id);
    printf("  Compiler / Cfg:  %s (%s)\n", BENCH_COMPILER, BENCH_CFLAGS);
    printf("  Matrix Mode:     %s\n", matrix_mode);
    printf("  Datasets:        %zu corpus files\n", dataset_count);
    printf("  Buffer Sizes:    %zu sizes\n", buffer_size_count);
    printf("  Policies:        %zu policies (Baseline 16/32/64 + PLSB P75/EWMA/Max/Weighted/Markov)\n", NUM_POLICIES);
    printf("  Iterations:      %d per cell (Total trials: %zu)\n", iterations, dataset_count * buffer_size_count * NUM_POLICIES * (size_t)iterations);
    printf("  Output JSON:     %s\n", custom_json);
    printf("  Output Raw CSV:  %s\n", custom_raw_csv);
    printf("  Output Sum CSV:  %s\n", custom_summary_csv);
    printf("===============================================================================================================\n\n");

    size_t max_summaries = dataset_count * buffer_size_count * NUM_POLICIES;
    bench_summary_t *all_summaries = (bench_summary_t *)malloc(max_summaries * sizeof(bench_summary_t));
    bench_observation_t *obs_buffer = (bench_observation_t *)malloc((size_t)iterations * sizeof(bench_observation_t));

    if (!all_summaries || !obs_buffer) {
        fprintf(stderr, "Fatal memory allocation failure allocating summary buffers\n");
        free(all_summaries);
        free(obs_buffer);
        if (single_buffer_size > 0) free((void *)buffer_sizes);
        free(datasets);
        return 1;
    }

    /* Initialize CSV files with headers unless appending */
    if (!append_csv) {
        FILE *rf = fopen(custom_raw_csv, "w");
        if (rf) {
            fprintf(rf, "experiment_id,compiler,cflags,dataset,source_bytes,buffer_size,policy,policy_id,init_cap,iteration,"
                       "status,wall_time_sec,tokens,spill_events,reallocations,copy_bytes,peak_capacity,over_res_bytes,"
                       "refills,hits,misses,tokens_per_sec,mb_per_sec\n");
            fclose(rf);
        }
        FILE *sf = fopen(custom_summary_csv, "w");
        if (sf) {
            fprintf(sf, "experiment_id,compiler,cflags,dataset,source_bytes,buffer_size,policy,policy_id,init_cap,"
                       "planned_iterations,successful_iterations,failed_iterations,correctness_status,"
                       "wall_ms_mean,wall_ms_std,wall_ms_median,wall_ms_min,wall_ms_max,"
                       "ktok_mean,ktok_std,mb_mean,mb_std,"
                       "spill_events_mean,reallocs_mean,reallocs_std,reallocs_median,"
                       "copy_bytes_mean,copy_bytes_std,copy_bytes_median,"
                       "peak_cap_mean,over_res_mean,refills_mean,hits_mean,misses_mean\n");
            fclose(sf);
        }
    }

    size_t summary_count = 0;
    bool all_benchmarks_passed = true;

    for (size_t d = 0; d < dataset_count; ++d) {
        const char *ds = datasets[d];

        for (size_t b = 0; b < buffer_size_count; ++b) {
            size_t bsize = buffer_sizes[b];
            print_terminal_table_header(ds, bsize, iterations);

            for (size_t p = 0; p < NUM_POLICIES; ++p) {
                const bench_policy_cfg_t *pcfg = &POLICIES[p];
                bench_summary_t *summ = &all_summaries[summary_count];

                bool ok = run_single_policy_benchmark(exp_id, ds, bsize, pcfg, iterations, obs_buffer, summ);
                if (ok) {
                    print_terminal_table_row(summ);
                    append_raw_csv(custom_raw_csv, exp_id, ds, bsize, pcfg, obs_buffer, iterations);
                    append_summary_csv(custom_summary_csv, summ);
                    summary_count++;
                } else {
                    printf(" %-26s  [VERIFICATION MISMATCH / EXECUTION ABORT]\n", pcfg->name);
                    all_benchmarks_passed = false;
                    /* Record failed configuration in raw and summary CSV */
                    append_raw_csv(custom_raw_csv, exp_id, ds, bsize, pcfg, obs_buffer, iterations);
                    append_summary_csv(custom_summary_csv, summ);
                    summary_count++;
                }
            }
            printf("===============================================================================================================\n\n");
        }
    }

    /* Write JSON results */
    bool json_ok = write_full_json_report(custom_json, exp_id, matrix_mode, all_summaries, summary_count);
    if (!json_ok) {
        fprintf(stderr, "  [ERROR] Writing JSON report to %s failed\n", custom_json);
        all_benchmarks_passed = false;
    } else {
        printf("  [OK] Exported full experiment JSON to %s\n", custom_json);
    }
    printf("  [OK] %s raw observations to %s\n", append_csv ? "Appended" : "Saved", custom_raw_csv);
    printf("  [OK] %s summary metrics to %s\n", append_csv ? "Appended" : "Saved", custom_summary_csv);

    if (!all_benchmarks_passed) {
        printf("\n  [FAIL] One or more benchmark configurations failed!\n\n");
    } else {
        printf("\n  [PASS] All %zu benchmark configurations completed successfully with 100%% equivalence!\n\n", summary_count);
    }

    free(obs_buffer);
    free(all_summaries);
    if (single_buffer_size > 0) free((void *)buffer_sizes);
    free(datasets);

    return all_benchmarks_passed ? 0 : 1;
}

#ifndef PLSB_BENCHMARK_NO_MAIN
int main(int argc, char **argv) {
    return run_benchmark_suite(argc, argv);
}
#endif
