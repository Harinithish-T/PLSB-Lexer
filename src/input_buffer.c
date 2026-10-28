#define _CRT_SECURE_NO_WARNINGS
#include "input_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int input_buffer_init_file(input_buffer_t *ib, const char *filepath, size_t buffer_size) {
    if (!ib || !filepath || buffer_size == 0 || buffer_size > SIZE_MAX - 1) return -1;
    memset(ib, 0, sizeof(*ib));
    
    ib->file = fopen(filepath, "rb");
    if (!ib->file) {
        ib->io_error = true;
        return -1;
    }
    
    ib->src_type = INPUT_SRC_FILE;
    strncpy(ib->source_name, filepath, sizeof(ib->source_name) - 1);
    ib->buffer_size = buffer_size;
    
    ib->buffers[0] = (char *)malloc(buffer_size + 1);
    ib->buffers[1] = (char *)malloc(buffer_size + 1);
    if (!ib->buffers[0] || !ib->buffers[1]) {
        input_buffer_free(ib);
        return -2;
    }
    
    /* Pre-fill initial buffer 0 */
    ib->active_buffer = 0;
    input_buffer_refill(ib, 0);
    if (ib->io_error) {
        input_buffer_free(ib);
        return -1;
    }
    ib->forward = ib->buffers[0];
    ib->lexeme_start = ib->buffers[0];
    return 0;
}

int input_buffer_init_stream(input_buffer_t *ib, FILE *stream, size_t buffer_size, const char *source_name) {
    if (!ib || !stream || buffer_size == 0 || buffer_size > SIZE_MAX - 1) return -1;
    memset(ib, 0, sizeof(*ib));
    
    ib->file = stream;
    ib->src_type = INPUT_SRC_STREAM;
    if (source_name) {
        strncpy(ib->source_name, source_name, sizeof(ib->source_name) - 1);
    } else {
        strcpy(ib->source_name, "<stream>");
    }
    ib->buffer_size = buffer_size;
    
    ib->buffers[0] = (char *)malloc(buffer_size + 1);
    ib->buffers[1] = (char *)malloc(buffer_size + 1);
    if (!ib->buffers[0] || !ib->buffers[1]) {
        input_buffer_free(ib);
        return -2;
    }
    
    ib->active_buffer = 0;
    input_buffer_refill(ib, 0);
    if (ib->io_error) {
        input_buffer_free(ib);
        return -1;
    }
    ib->forward = ib->buffers[0];
    ib->lexeme_start = ib->buffers[0];
    return 0;
}

int input_buffer_init_string(input_buffer_t *ib, const char *str, size_t len, size_t buffer_size) {
    if (!ib || !str || buffer_size == 0 || buffer_size > SIZE_MAX - 1) return -1;
    memset(ib, 0, sizeof(*ib));
    
    ib->src_type = INPUT_SRC_STRING;
    ib->src_string = str;
    ib->src_string_len = len;
    ib->src_string_offset = 0;
    strcpy(ib->source_name, "<string>");
    ib->buffer_size = buffer_size;
    
    ib->buffers[0] = (char *)malloc(buffer_size + 1);
    ib->buffers[1] = (char *)malloc(buffer_size + 1);
    if (!ib->buffers[0] || !ib->buffers[1]) {
        input_buffer_free(ib);
        return -2;
    }
    
    ib->active_buffer = 0;
    input_buffer_refill(ib, 0);
    ib->forward = ib->buffers[0];
    ib->lexeme_start = ib->buffers[0];
    return 0;
}

void input_buffer_free(input_buffer_t *ib) {
    if (!ib) return;
    if (ib->src_type == INPUT_SRC_FILE && ib->file) {
        fclose(ib->file);
        ib->file = NULL;
    }
    if (ib->buffers[0]) {
        free(ib->buffers[0]);
        ib->buffers[0] = NULL;
    }
    if (ib->buffers[1]) {
        free(ib->buffers[1]);
        ib->buffers[1] = NULL;
    }
    ib->forward = NULL;
    ib->lexeme_start = NULL;
}

size_t input_buffer_refill(input_buffer_t *ib, int target_buf) {
    if (!ib || target_buf < 0 || target_buf > 1) return 0;
    if (!ib->buffers[target_buf]) return 0;
    if (ib->eof_reached || ib->io_error) {
        ib->bytes_read[target_buf] = 0;
        ib->buffers[target_buf][0] = '\0';
        return 0;
    }
    
    size_t bytes = 0;
    if (ib->src_type == INPUT_SRC_STRING) {
        size_t remaining = ib->src_string_len - ib->src_string_offset;
        bytes = (remaining > ib->buffer_size) ? ib->buffer_size : remaining;
        if (bytes > 0) {
            memcpy(ib->buffers[target_buf], ib->src_string + ib->src_string_offset, bytes);
            ib->src_string_offset += bytes;
        }
        if (ib->src_string_offset >= ib->src_string_len) {
            ib->eof_reached = true;
        }
    } else {
        bytes = fread(ib->buffers[target_buf], 1, ib->buffer_size, ib->file);
        if (bytes < ib->buffer_size) {
            if (ferror(ib->file)) {
                ib->io_error = true;
            } else {
                ib->eof_reached = true;
            }
        }
    }
    
    ib->bytes_read[target_buf] = bytes;
    /* Put boundary sentinel immediately following the read chunk */
    ib->buffers[target_buf][bytes] = '\0';
    ib->total_bytes_read += bytes;
    ib->refill_count++;
    return bytes;
}
