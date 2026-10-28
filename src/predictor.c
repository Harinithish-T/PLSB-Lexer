#include "predictor.h"
#include <string.h>
#include <math.h>

const char *token_class_name(token_class_t c) {
    switch (c) {
        case TOKEN_CLASS_IDENTIFIER: return "Identifier";
        case TOKEN_CLASS_NUMBER:     return "Number";
        case TOKEN_CLASS_STRING:     return "String";
        case TOKEN_CLASS_CHAR:       return "Char";
        case TOKEN_CLASS_OPERATOR:   return "Operator";
        case TOKEN_CLASS_PUNCTUATION:return "Punctuation";
        case TOKEN_CLASS_COMMENT:    return "Comment";
        case TOKEN_CLASS_OTHER:      return "Other";
        default:                     return "Unknown";
    }
}

void predictor_init_default(predictor_t *p) {
    predictor_config_t cfg;
    cfg.rule = PRED_RULE_COMBINED;
    cfg.ewma_alpha = 0.20;
    cfg.weighted_p75_weight = 0.70;
    cfg.safe_min_capacity = 16;
    cfg.max_capacity = 4096;
    cfg.min_samples_for_p75 = 5;
    cfg.baseline_init_capacity = 16;
    predictor_init(p, &cfg);
}

void predictor_init(predictor_t *p, const predictor_config_t *cfg) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    if (cfg) {
        p->config = *cfg;
    } else {
        predictor_init_default(p);
    }
}

void predictor_reset(predictor_t *p) {
    if (!p) return;
    predictor_config_t cfg = p->config;
    memset(p, 0, sizeof(*p));
    p->config = cfg;
}

void predictor_update(predictor_t *p, token_class_t token_class, size_t length) {
    if (!p || (int)token_class < 0 || token_class >= TOKEN_CLASS_COUNT) return;
    
    class_length_stats_t *st = &p->class_stats[token_class];
    if (st->sample_count == 0) {
        st->min_length = length;
        st->max_length = length;
        st->ewma_length = (double)length;
    } else {
        if (length < st->min_length) st->min_length = length;
        if (length > st->max_length) st->max_length = length;
        st->ewma_length = p->config.ewma_alpha * (double)length + (1.0 - p->config.ewma_alpha) * st->ewma_length;
    }
    st->sample_count++;
    st->total_length += length;
    
    size_t hist_idx = (length < PREDICTOR_HIST_MAX) ? length : (PREDICTOR_HIST_MAX - 1);
    st->length_hist[hist_idx]++;
    
    /* Update Markov context transitions */
    if (p->has_last_class && (int)p->last_class >= 0 && p->last_class < TOKEN_CLASS_COUNT) {
        p->transition_matrix[p->last_class][token_class]++;
        
        uint64_t ccount = p->cond_count[p->last_class][token_class];
        if (ccount == 0) {
            p->cond_ewma[p->last_class][token_class] = (double)length;
        } else {
            p->cond_ewma[p->last_class][token_class] = 
                p->config.ewma_alpha * (double)length + 
                (1.0 - p->config.ewma_alpha) * p->cond_ewma[p->last_class][token_class];
        }
        p->cond_count[p->last_class][token_class]++;
    }
    
    p->last_class = token_class;
    p->has_last_class = true;
}

size_t predictor_get_p75(const predictor_t *p, token_class_t token_class) {
    if (!p || (int)token_class < 0 || token_class >= TOKEN_CLASS_COUNT) return 0;
    const class_length_stats_t *st = &p->class_stats[token_class];
    if (st->sample_count < p->config.min_samples_for_p75) return 0;
    
    uint64_t target = (uint64_t)ceil(0.75 * (double)st->sample_count);
    uint64_t accum = 0;
    for (size_t i = 0; i < PREDICTOR_HIST_MAX; ++i) {
        accum += st->length_hist[i];
        if (accum >= target) {
            return i;
        }
    }
    return st->max_length;
}

size_t predictor_predict_capacity(const predictor_t *p, token_class_t candidate_class, size_t current_scanned_bytes) {
    if (!p) return 16;
    
    size_t pred = 0;
    const class_length_stats_t *st = &p->class_stats[candidate_class];
    
    switch (p->config.rule) {
        case PRED_RULE_BASELINE:
            pred = p->config.baseline_init_capacity;
            break;
            
        case PRED_RULE_P75_ONLY: {
            size_t p75 = predictor_get_p75(p, candidate_class);
            pred = (p75 > 0) ? p75 : p->config.safe_min_capacity;
            break;
        }
        
        case PRED_RULE_EWMA_ONLY: {
            if (st->sample_count > 0) {
                pred = (size_t)ceil(st->ewma_length);
            } else {
                pred = p->config.safe_min_capacity;
            }
            break;
        }
        
        case PRED_RULE_MAX_P75_EWMA: {
            size_t p75 = predictor_get_p75(p, candidate_class);
            size_t ew = (st->sample_count > 0) ? (size_t)ceil(st->ewma_length) : 0;
            pred = (p75 > ew) ? p75 : ew;
            if (pred == 0) pred = p->config.safe_min_capacity;
            break;
        }
        
        case PRED_RULE_WEIGHTED: {
            size_t p75 = predictor_get_p75(p, candidate_class);
            double ew = (st->sample_count > 0) ? st->ewma_length : (double)p->config.safe_min_capacity;
            double p75_val = (p75 > 0) ? (double)p75 : ew;
            double w = p->config.weighted_p75_weight;
            pred = (size_t)ceil(w * p75_val + (1.0 - w) * ew);
            break;
        }
        
        case PRED_RULE_MARKOV_CONDITIONED: {
            if (p->has_last_class && p->cond_count[p->last_class][candidate_class] > 0) {
                pred = (size_t)ceil(p->cond_ewma[p->last_class][candidate_class]);
            } else {
                size_t p75 = predictor_get_p75(p, candidate_class);
                size_t ew = (st->sample_count > 0) ? (size_t)ceil(st->ewma_length) : 0;
                pred = (p75 > ew) ? p75 : ew;
            }
            if (pred == 0) pred = p->config.safe_min_capacity;
            break;
        }

        /*
         * Combined PLSB capacity (production rule).
         *
         * Signals available at allocation time for candidate class c:
         *   Q_t = P75(c) if N_c >= N_min, else 0
         *   E_t = ceil(EWMA(c)) if N_c > 0, else 0
         *   M_t = ceil(cond_EWMA[prev][c]) if transition (prev -> c) was
         *         observed on already-finished tokens, else 0
         *
         * M_t is a length estimate for that class transition, not a guess of
         * the next token type. Future tokens are never used.
         *
         *   C_raw = max(Q_t, E_t, M_t)
         *   if C_raw == 0: C_raw = C_safe_min
         *   C_t = clamp(max(C_raw, L_scanned + 1), C_min, C_max)
         */
        case PRED_RULE_COMBINED: {
            size_t q = predictor_get_p75(p, candidate_class);
            size_t e = (st->sample_count > 0) ? (size_t)ceil(st->ewma_length) : 0;
            size_t m = 0;
            if (p->has_last_class &&
                (int)p->last_class >= 0 && p->last_class < TOKEN_CLASS_COUNT &&
                (int)candidate_class >= 0 && candidate_class < TOKEN_CLASS_COUNT &&
                p->cond_count[p->last_class][candidate_class] > 0) {
                m = (size_t)ceil(p->cond_ewma[p->last_class][candidate_class]);
            }
            pred = q;
            if (e > pred) pred = e;
            if (m > pred) pred = m;
            if (pred == 0) pred = p->config.safe_min_capacity;
            break;
        }
        
        default:
            pred = p->config.safe_min_capacity;
            break;
    }
    
    /* Enforce safe bounds */
    if (pred < p->config.safe_min_capacity) {
        pred = p->config.safe_min_capacity;
    }
    if (pred > p->config.max_capacity) {
        pred = p->config.max_capacity;
    }
    /* Ensure it accommodates what is already known scanned */
    if (pred < current_scanned_bytes + 1) {
        pred = current_scanned_bytes + 1;
    }
    
    return pred;
}

double predictor_get_markov_prob(const predictor_t *p, token_class_t from, token_class_t to) {
    if (!p || (int)from < 0 || from >= TOKEN_CLASS_COUNT || (int)to < 0 || to >= TOKEN_CLASS_COUNT) {
        return 1.0 / (double)TOKEN_CLASS_COUNT;
    }
    uint64_t total = 0;
    for (int i = 0; i < TOKEN_CLASS_COUNT; ++i) {
        total += p->transition_matrix[from][i];
    }
    if (total == 0) {
        return 1.0 / (double)TOKEN_CLASS_COUNT;
    }
    return (double)p->transition_matrix[from][to] / (double)total;
}
