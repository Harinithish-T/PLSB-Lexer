#ifndef PLSB_SPILL_BUFFER_H
#define PLSB_SPILL_BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "predictor.h"
#include "statistics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPILL_POLICY_BASELINE = 0,
    SPILL_POLICY_PLSB     = 1
} spill_policy_t;

typedef void *(*plsb_malloc_fn)(size_t size);
typedef void (*plsb_free_fn)(void *ptr);

void spill_buffer_set_allocator(plsb_malloc_fn malloc_fn, plsb_free_fn free_fn);
plsb_malloc_fn spill_buffer_get_malloc(void);
plsb_free_fn spill_buffer_get_free(void);

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    
    spill_policy_t policy;
    size_t baseline_init_capacity;
    
    predictor_t *predictor;
    statistics_t *stats;
    
    token_class_t candidate_class;
    bool is_active;
    bool had_realloc;
} spill_buffer_t;

void spill_buffer_init(spill_buffer_t *sb, spill_policy_t policy, size_t baseline_init_cap, 
                       predictor_t *predictor, statistics_t *stats);

/**
 * Initializes spill storage for a candidate token class.
 * Returns 0 on success, -1 on allocation failure.
 */
int spill_buffer_begin(spill_buffer_t *sb, token_class_t candidate_class, size_t initial_bytes);

/**
 * Appends bytes to the spill buffer, expanding geometrically if required.
 * Returns 0 on success, -1 on allocation failure or size overflow.
 */
int spill_buffer_append(spill_buffer_t *sb, const char *bytes, size_t count);

/**
 * Finalizes the spilled token, transfers ownership of the string to caller,
 * and updates prediction hit/miss and over-reservation statistics.
 */
char *spill_buffer_finish(spill_buffer_t *sb, size_t *out_len);

void spill_buffer_reset(spill_buffer_t *sb);

void spill_buffer_free(spill_buffer_t *sb);

#ifdef __cplusplus
}
#endif

#endif /* PLSB_SPILL_BUFFER_H */
