#include "statistics.h"
#include <string.h>

void statistics_init(statistics_t *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

void statistics_reset(statistics_t *s) {
    statistics_init(s);
}

void statistics_finalize(statistics_t *s, const predictor_t *p, double wall_sec, double cpu_sec) {
    if (!s) return;
    s->wall_time_seconds = wall_sec;
    s->cpu_time_seconds = cpu_sec;
    
    if (wall_sec > 0.0) {
        s->tokens_per_second = (double)s->total_tokens / wall_sec;
        s->bytes_per_second = (double)s->total_source_bytes / wall_sec;
    }
    
    if (p) {
        for (int c = 0; c < TOKEN_CLASS_COUNT; ++c) {
            const class_length_stats_t *st = &p->class_stats[c];
            s->class_counts[c] = st->sample_count;
            s->class_min_len[c] = st->min_length;
            s->class_max_len[c] = st->max_length;
            s->class_total_len[c] = st->total_length;
            s->class_ewma_len[c] = st->ewma_length;
            s->class_p75_len[c] = predictor_get_p75(p, (token_class_t)c);
            
            for (int d = 0; d < TOKEN_CLASS_COUNT; ++d) {
                s->transition_matrix[c][d] = p->transition_matrix[c][d];
                s->markov_probabilities[c][d] = predictor_get_markov_prob(p, (token_class_t)c, (token_class_t)d);
            }
        }
    }
}
