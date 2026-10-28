#ifndef PLSB_PREDICTOR_H
#define PLSB_PREDICTOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOKEN_CLASS_COUNT 8
#define PREDICTOR_HIST_MAX 4096

typedef enum {
    TOKEN_CLASS_IDENTIFIER = 0,
    TOKEN_CLASS_NUMBER     = 1,
    TOKEN_CLASS_STRING     = 2,
    TOKEN_CLASS_CHAR       = 3,
    TOKEN_CLASS_OPERATOR   = 4,
    TOKEN_CLASS_PUNCTUATION= 5,
    TOKEN_CLASS_COMMENT    = 6,
    TOKEN_CLASS_OTHER      = 7
} token_class_t;

const char *token_class_name(token_class_t c);

typedef enum {
    PRED_RULE_BASELINE           = 0, /* Fixed initial capacity */
    PRED_RULE_P75_ONLY           = 1, /* 75th percentile of token class length (ablation) */
    PRED_RULE_EWMA_ONLY          = 2, /* Exponential moving average of token class (ablation) */
    PRED_RULE_MAX_P75_EWMA       = 3, /* Conservative max(P75, EWMA) (ablation) */
    PRED_RULE_WEIGHTED           = 4, /* Convex combination: w*P75 + (1-w)*EWMA (ablation) */
    PRED_RULE_MARKOV_CONDITIONED = 5, /* Transition-conditioned length estimate (ablation) */
    PRED_RULE_COMBINED           = 6  /* Production PLSB: clamp(max(Q, E, M), Cmin, Cmax) */
} prediction_rule_t;

typedef struct {
    prediction_rule_t rule;
    double ewma_alpha;             /* Smoothing factor alpha (e.g. 0.20) */
    double weighted_p75_weight;    /* Weight w in [0, 1] for P75 (e.g. 0.70) */
    size_t safe_min_capacity;      /* Minimum initial capacity clamp (e.g. 16) */
    size_t max_capacity;           /* Maximum initial capacity clamp (e.g. 4096) */
    size_t min_samples_for_p75;    /* Observations required before P75 is trusted */
    size_t baseline_init_capacity; /* Initial capacity used in baseline mode */
} predictor_config_t;

typedef struct {
    uint64_t sample_count;
    size_t min_length;
    size_t max_length;
    uint64_t total_length;
    double ewma_length;
    uint32_t length_hist[PREDICTOR_HIST_MAX];
} class_length_stats_t;

typedef struct {
    predictor_config_t config;
    
    /* Per-class empirical statistics */
    class_length_stats_t class_stats[TOKEN_CLASS_COUNT];
    
    /* Markov transition matrix: transitions[from][to] */
    uint64_t transition_matrix[TOKEN_CLASS_COUNT][TOKEN_CLASS_COUNT];
    
    /* Markov context-conditioned EWMA: cond_ewma[from][to] */
    double cond_ewma[TOKEN_CLASS_COUNT][TOKEN_CLASS_COUNT];
    uint64_t cond_count[TOKEN_CLASS_COUNT][TOKEN_CLASS_COUNT];
    
    token_class_t last_class;
    bool has_last_class;
} predictor_t;

/**
 * Initialize predictor with default configuration.
 */
void predictor_init_default(predictor_t *p);

/**
 * Initialize predictor with a custom configuration.
 */
void predictor_init(predictor_t *p, const predictor_config_t *cfg);

/**
 * Reset predictor state (clears all statistics, keeps configuration).
 */
void predictor_reset(predictor_t *p);

/**
 * Update predictor with an observed completed token.
 */
void predictor_update(predictor_t *p, token_class_t token_class, size_t length);

/**
 * Compute the empirical 75th percentile token length for a class.
 * Returns 0 if insufficient samples are available.
 */
size_t predictor_get_p75(const predictor_t *p, token_class_t token_class);

/**
 * Predict initial spill buffer capacity for a candidate token class.
 * candidate_class: token class currently being scanned.
 * current_scanned_bytes: bytes already accumulated before spill was triggered.
 */
size_t predictor_predict_capacity(const predictor_t *p, token_class_t candidate_class, size_t current_scanned_bytes);

/**
 * Get Markov transition probability P(to | from).
 * If transitions from 'from' is 0, returns uniform (1.0 / TOKEN_CLASS_COUNT).
 */
double predictor_get_markov_prob(const predictor_t *p, token_class_t from, token_class_t to);

#ifdef __cplusplus
}
#endif

#endif /* PLSB_PREDICTOR_H */
