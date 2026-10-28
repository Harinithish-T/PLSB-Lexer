#ifndef PLSB_INPUT_BUFFER_H
#define PLSB_INPUT_BUFFER_H

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLSB_DEFAULT_INPUT_BUFFER_SIZE 4096

typedef enum {
    INPUT_SRC_FILE,
    INPUT_SRC_STREAM,
    INPUT_SRC_STRING
} input_source_type_t;

/**
 * Double-buffer input system with boundary sentinels.
 *
 * Ownership Rules:
 * - When initialized with input_buffer_init_file(), the input_buffer owns the FILE* handle
 *   and will close it in input_buffer_free().
 * - When initialized with input_buffer_init_stream(), the caller owns the FILE* handle;
 *   input_buffer_free() will not close it.
 * - When initialized with input_buffer_init_string(), the caller owns the underlying string memory.
 * - input_buffer_free() is idempotent and safe to call repeatedly.
 */
typedef struct {
    char *buffers[2];            /* Two alternating buffer blocks */
    size_t buffer_size;          /* Capacity per buffer (excluding sentinel) */
    size_t bytes_read[2];        /* Valid bytes loaded in each buffer */
    int active_buffer;           /* 0 or 1 */
    
    char *forward;               /* Pointer to current scan position */
    char *lexeme_start;          /* Pointer to start of current lexeme in active buffer */
    
    /* Source stream state */
    input_source_type_t src_type;
    FILE *file;
    const char *src_string;
    size_t src_string_len;
    size_t src_string_offset;
    char source_name[256];
    
    bool eof_reached;            /* True if source has reached EOF */
    bool io_error;               /* True if read error occurred */
    size_t total_bytes_read;     /* Cumulative bytes read across all refills */
    size_t refill_count;         /* Number of buffer refill operations executed */
} input_buffer_t;

/**
 * Initialize input buffer reading from a file path.
 * Returns 0 on success, -1 on I/O open error, -2 on memory allocation failure.
 */
int input_buffer_init_file(input_buffer_t *ib, const char *filepath, size_t buffer_size);

/**
 * Initialize input buffer reading from an open FILE* stream.
 * Caller retains ownership of the stream.
 * Returns 0 on success, -1 on invalid parameter, -2 on memory allocation failure.
 */
int input_buffer_init_stream(input_buffer_t *ib, FILE *stream, size_t buffer_size, const char *source_name);

/**
 * Initialize input buffer reading from an in-memory string.
 * Used for unit testing, integration tests, and edge case evaluation.
 * Returns 0 on success, -1 on invalid parameter, -2 on memory allocation failure.
 */
int input_buffer_init_string(input_buffer_t *ib, const char *str, size_t len, size_t buffer_size);

/**
 * Free memory associated with the input buffer and close owned files.
 * Idempotent: safe to call multiple times.
 */
void input_buffer_free(input_buffer_t *ib);

/**
 * Refill the specified buffer index (0 or 1).
 * Returns number of bytes read, or 0 on EOF or error.
 */
size_t input_buffer_refill(input_buffer_t *ib, int target_buf);

#ifdef __cplusplus
}
#endif

#endif /* PLSB_INPUT_BUFFER_H */
