#include "spill_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static plsb_malloc_fn g_malloc_fn = malloc;
static plsb_free_fn g_free_fn = free;

void spill_buffer_set_allocator(plsb_malloc_fn malloc_fn, plsb_free_fn free_fn) {
    g_malloc_fn = malloc_fn ? malloc_fn : malloc;
    g_free_fn = free_fn ? free_fn : free;
}

plsb_malloc_fn spill_buffer_get_malloc(void) {
    return g_malloc_fn;
}

plsb_free_fn spill_buffer_get_free(void) {
    return g_free_fn;
}

void spill_buffer_init(spill_buffer_t *sb, spill_policy_t policy, size_t baseline_init_cap, 
                       predictor_t *predictor, statistics_t *stats) {
    if (!sb) return;
    memset(sb, 0, sizeof(*sb));
    sb->policy = policy;
    sb->baseline_init_capacity = (baseline_init_cap > 0) ? baseline_init_cap : 16;
    sb->predictor = predictor;
    sb->stats = stats;
    sb->is_active = false;
}

int spill_buffer_begin(spill_buffer_t *sb, token_class_t candidate_class, size_t initial_bytes) {
    if (!sb) return -1;
    
    /* Clean up any lingering data */
    if (sb->data) {
        g_free_fn(sb->data);
        sb->data = NULL;
    }
    sb->length = 0;
    sb->capacity = 0;
    sb->candidate_class = candidate_class;
    sb->had_realloc = false;
    sb->is_active = false;
    
    if (initial_bytes > SIZE_MAX - 1) {
        return -1; /* Overflow check */
    }
    
    size_t cap = 0;
    if (sb->policy == SPILL_POLICY_BASELINE) {
        cap = (initial_bytes + 1 > sb->baseline_init_capacity) ? (initial_bytes + 1) : sb->baseline_init_capacity;
    } else {
        if (sb->predictor) {
            cap = predictor_predict_capacity(sb->predictor, candidate_class, initial_bytes);
        } else {
            cap = sb->baseline_init_capacity;
        }
        if (sb->stats) {
            sb->stats->prediction_triggers++;
        }
    }
    
    if (cap < 16) cap = 16;
    if (cap < initial_bytes + 1) cap = initial_bytes + 1;
    
    sb->data = (char *)g_malloc_fn(cap);
    if (!sb->data) {
        /* Allocation failure: do not update stats as though it succeeded */
        return -1;
    }
    
    sb->capacity = cap;
    sb->data[0] = '\0';
    sb->is_active = true;
    
    if (sb->stats) {
        sb->stats->lexeme_spill_events++;
        sb->stats->spill_allocations++;
        sb->stats->total_requested_capacity += cap;
        if (cap > sb->stats->peak_spill_capacity) {
            sb->stats->peak_spill_capacity = cap;
        }
    }
    return 0;
}

int spill_buffer_append(spill_buffer_t *sb, const char *bytes, size_t count) {
    if (!sb || !bytes || count == 0) return 0;
    if (!sb->is_active || !sb->data) return -1;
    
    /* Arithmetic overflow check: length + count + 1 */
    if (count > SIZE_MAX - sb->length - 1) {
        return -1;
    }
    
    size_t needed = sb->length + count + 1;
    if (needed > sb->capacity) {
        /* Geometric doubling expansion with SIZE_MAX overflow protection */
        size_t new_cap = (sb->capacity > 0) ? sb->capacity : 16;
        while (new_cap < needed) {
            if (new_cap > SIZE_MAX / 2) {
                new_cap = needed;
                break;
            }
            new_cap *= 2;
        }
        
        char *new_data = (char *)g_malloc_fn(new_cap);
        if (!new_data) {
            /* Allocation failure reported: retain old buffer and abort */
            return -1;
        }
        
        if (sb->length > 0) {
            memcpy(new_data, sb->data, sb->length);
        }
        g_free_fn(sb->data);
        sb->data = new_data;
        
        if (sb->stats) {
            sb->stats->spill_reallocations++;
            sb->stats->growth_copy_bytes += sb->length;
            sb->stats->total_requested_capacity += new_cap;
            if (new_cap > sb->stats->peak_spill_capacity) {
                sb->stats->peak_spill_capacity = new_cap;
            }
        }
        
        sb->capacity = new_cap;
        sb->had_realloc = true;
    }
    
    memcpy(sb->data + sb->length, bytes, count);
    sb->length += count;
    sb->data[sb->length] = '\0';
    return 0;
}

char *spill_buffer_finish(spill_buffer_t *sb, size_t *out_len) {
    if (!sb || !sb->is_active || !sb->data) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    if (sb->policy == SPILL_POLICY_PLSB && sb->stats) {
        if (sb->had_realloc) {
            sb->stats->prediction_misses++;
        } else {
            sb->stats->prediction_hits++;
            if (sb->capacity > sb->length + 1) {
                sb->stats->over_reservation_bytes += (sb->capacity - (sb->length + 1));
            }
        }
    }
    
    char *res = sb->data;
    if (out_len) *out_len = sb->length;
    
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    sb->is_active = false;
    sb->had_realloc = false;
    
    return res;
}

void spill_buffer_reset(spill_buffer_t *sb) {
    if (!sb) return;
    if (sb->data) {
        g_free_fn(sb->data);
        sb->data = NULL;
    }
    sb->length = 0;
    sb->capacity = 0;
    sb->is_active = false;
    sb->had_realloc = false;
}

void spill_buffer_free(spill_buffer_t *sb) {
    spill_buffer_reset(sb);
}
