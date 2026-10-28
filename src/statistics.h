#ifndef PLSB_STATISTICS_H
#define PLSB_STATISTICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "predictor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Token counts and classifications */
    uint64_t total_tokens;
    uint64_t class_counts[TOKEN_CLASS_COUNT];
    
    /* Length statistics per class */
    size_t class_min_len[TOKEN_CLASS_COUNT];
    size_t class_max_len[TOKEN_CLASS_COUNT];
    uint64_t class_total_len[TOKEN_CLASS_COUNT];
    size_t class_p75_len[TOKEN_CLASS_COUNT];
    double class_ewma_len[TOKEN_CLASS_COUNT];
    
    /* Markov transition matrix */
    uint64_t transition_matrix[TOKEN_CLASS_COUNT][TOKEN_CLASS_COUNT];
    double markov_probabilities[TOKEN_CLASS_COUNT][TOKEN_CLASS_COUNT];
    
    /* Input buffer metrics */
    uint64_t total_source_bytes;
    uint64_t input_refill_count;
    
    /* Spill buffer metrics */
    uint64_t lexeme_spill_events;        /* Tokens crossing buffer boundaries */
    uint64_t spill_allocations;          /* Initial allocations for spill buffers */
    uint64_t spill_reallocations;        /* Geometric reallocations */
    uint64_t growth_copy_bytes;          /* Bytes copied strictly due to buffer growth */
    uint64_t total_requested_capacity;   /* Cumulative memory requested */
    size_t peak_spill_capacity;          /* Peak capacity observed in spill buffer */
    
    /* Predictor evaluation metrics */
    uint64_t prediction_triggers;        /* Times initial capacity was predicted */
    uint64_t prediction_hits;            /* Predicted capacity avoided realloc */
    uint64_t prediction_misses;          /* Reallocation occurred despite prediction */
    uint64_t over_reservation_bytes;     /* Excess capacity reserved */
    
    /* Execution timing */
    double wall_time_seconds;
    double cpu_time_seconds;
    double tokens_per_second;
    double bytes_per_second;
} statistics_t;

void statistics_init(statistics_t *s);
void statistics_reset(statistics_t *s);

/**
 * Finalize statistics at the end of a lexical analysis run,
 * synchronizing with predictor state and computing rates and distributions.
 */
void statistics_finalize(statistics_t *s, const predictor_t *p, double wall_sec, double cpu_sec);

#ifdef __cplusplus
}
#endif

#endif /* PLSB_STATISTICS_H */
