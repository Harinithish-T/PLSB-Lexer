#include "report.h"
#include <inttypes.h>

void report_print_summary(FILE *out, const statistics_t *s, const char *label) {
    if (!out || !s) return;
    fprintf(out, "======================================================================\n");
    fprintf(out, "  LEXICAL ANALYSIS REPORT: %s\n", label ? label : "Standard Run");
    fprintf(out, "======================================================================\n");
    fprintf(out, "Input Source & Buffering:\n");
    fprintf(out, "  Total Source Bytes:       %" PRIu64 "\n", s->total_source_bytes);
    fprintf(out, "  Total Tokens Emitted:     %" PRIu64 "\n", s->total_tokens);
    fprintf(out, "  Input Buffer Refills:     %" PRIu64 "\n", s->input_refill_count);
    fprintf(out, "  Lexeme Spill Events:      %" PRIu64 "\n", s->lexeme_spill_events);
    fprintf(out, "\nSpill Memory Allocation & Copy Overhead:\n");
    fprintf(out, "  Initial Spill Allocations: %" PRIu64 "\n", s->spill_allocations);
    fprintf(out, "  Spill Reallocations:       %" PRIu64 "\n", s->spill_reallocations);
    fprintf(out, "  Growth-Copy Bytes:         %" PRIu64 " bytes\n", s->growth_copy_bytes);
    fprintf(out, "  Total Requested Capacity:  %" PRIu64 " bytes\n", s->total_requested_capacity);
    fprintf(out, "  Peak Spill Capacity:       %zu bytes\n", s->peak_spill_capacity);
    
    if (s->prediction_triggers > 0) {
        fprintf(out, "\nPredictor Diagnostics (PLSB):\n");
        fprintf(out, "  Prediction Triggers:       %" PRIu64 "\n", s->prediction_triggers);
        fprintf(out, "  Prediction Hits (0 realloc):%" PRIu64 " (%.1f%%)\n", 
                s->prediction_hits, 
                s->prediction_triggers ? (100.0 * (double)s->prediction_hits / (double)s->prediction_triggers) : 0.0);
        fprintf(out, "  Prediction Misses:         %" PRIu64 " (%.1f%%)\n",
                s->prediction_misses,
                s->prediction_triggers ? (100.0 * (double)s->prediction_misses / (double)s->prediction_triggers) : 0.0);
        fprintf(out, "  Over-Reservation Bytes:    %" PRIu64 " bytes\n", s->over_reservation_bytes);
    }
    
    fprintf(out, "\nExecution Timing & Throughput:\n");
    fprintf(out, "  Wall-Clock Time:           %.6f s (%.2f ms)\n", s->wall_time_seconds, s->wall_time_seconds * 1000.0);
    fprintf(out, "  CPU Time:                  %.6f s (%.2f ms)\n", s->cpu_time_seconds, s->cpu_time_seconds * 1000.0);
    fprintf(out, "  Token Throughput:          %.2f tokens/s\n", s->tokens_per_second);
    fprintf(out, "  Byte Throughput:           %.2f MB/s\n", s->bytes_per_second / (1024.0 * 1024.0));
    
    fprintf(out, "\nToken Class Distributions:\n");
    fprintf(out, "  %-14s %-10s %-8s %-8s %-10s %-8s %-8s\n",
            "Class", "Count", "Min", "Max", "Mean", "P75", "EWMA");
    fprintf(out, "  --------------------------------------------------------------------\n");
    for (int c = 0; c < TOKEN_CLASS_COUNT; ++c) {
        double mean = (s->class_counts[c] > 0) ? ((double)s->class_total_len[c] / (double)s->class_counts[c]) : 0.0;
        fprintf(out, "  %-14s %-10" PRIu64 " %-8zu %-8zu %-10.2f %-8zu %-8.2f\n",
                token_class_name((token_class_t)c),
                s->class_counts[c],
                s->class_min_len[c],
                s->class_max_len[c],
                mean,
                s->class_p75_len[c],
                s->class_ewma_len[c]);
    }
    fprintf(out, "======================================================================\n\n");
}

void report_print_comparison(FILE *out, const statistics_t *b, const statistics_t *p) {
    if (!out || !b || !p) return;
    fprintf(out, "======================================================================\n");
    fprintf(out, "  COMPARATIVE EXPERIMENT: Baseline vs PLSB\n");
    fprintf(out, "======================================================================\n");
    fprintf(out, "  Metric                           Baseline            PLSB                Delta (%%)\n");
    fprintf(out, "  --------------------------------------------------------------------\n");
    
    int64_t realloc_diff = (int64_t)p->spill_reallocations - (int64_t)b->spill_reallocations;
    double realloc_pct = b->spill_reallocations ? (100.0 * (double)realloc_diff / (double)b->spill_reallocations) : 0.0;
    
    int64_t copy_diff = (int64_t)p->growth_copy_bytes - (int64_t)b->growth_copy_bytes;
    double copy_pct = b->growth_copy_bytes ? (100.0 * (double)copy_diff / (double)b->growth_copy_bytes) : 0.0;
    
    fprintf(out, "  Spill Reallocations:             %-19" PRIu64 " %-19" PRIu64 " %+6.1f%%\n",
            b->spill_reallocations, p->spill_reallocations, realloc_pct);
    fprintf(out, "  Growth-Copy Bytes:               %-19" PRIu64 " %-19" PRIu64 " %+6.1f%%\n",
            b->growth_copy_bytes, p->growth_copy_bytes, copy_pct);
    fprintf(out, "  Peak Spill Capacity (bytes):     %-19zu %-19zu\n",
            b->peak_spill_capacity, p->peak_spill_capacity);
    fprintf(out, "  Total Requested Memory (bytes):  %-19" PRIu64 " %-19" PRIu64 "\n",
            b->total_requested_capacity, p->total_requested_capacity);
    fprintf(out, "  Buffer Refills:                  %-19" PRIu64 " %-19" PRIu64 "\n",
            b->input_refill_count, p->input_refill_count);
    fprintf(out, "  Lexeme Spill Events:             %-19" PRIu64 " %-19" PRIu64 "\n",
            b->lexeme_spill_events, p->lexeme_spill_events);
    fprintf(out, "  Wall Time (ms):                  %-19.3f %-19.3f\n",
            b->wall_time_seconds * 1000.0, p->wall_time_seconds * 1000.0);
    fprintf(out, "  Throughput (kTok/s):             %-19.2f %-19.2f\n",
            b->tokens_per_second / 1000.0, p->tokens_per_second / 1000.0);
    fprintf(out, "======================================================================\n\n");
}

void report_write_json(FILE *out, const statistics_t *s, const char *label) {
    if (!out || !s) return;
    fprintf(out, "{\n");
    fprintf(out, "  \"label\": \"%s\",\n", label ? label : "Run");
    fprintf(out, "  \"total_source_bytes\": %" PRIu64 ",\n", s->total_source_bytes);
    fprintf(out, "  \"total_tokens\": %" PRIu64 ",\n", s->total_tokens);
    fprintf(out, "  \"input_refill_count\": %" PRIu64 ",\n", s->input_refill_count);
    fprintf(out, "  \"lexeme_spill_events\": %" PRIu64 ",\n", s->lexeme_spill_events);
    fprintf(out, "  \"spill_allocations\": %" PRIu64 ",\n", s->spill_allocations);
    fprintf(out, "  \"spill_reallocations\": %" PRIu64 ",\n", s->spill_reallocations);
    fprintf(out, "  \"growth_copy_bytes\": %" PRIu64 ",\n", s->growth_copy_bytes);
    fprintf(out, "  \"total_requested_capacity\": %" PRIu64 ",\n", s->total_requested_capacity);
    fprintf(out, "  \"peak_spill_capacity\": %zu,\n", s->peak_spill_capacity);
    fprintf(out, "  \"prediction_triggers\": %" PRIu64 ",\n", s->prediction_triggers);
    fprintf(out, "  \"prediction_hits\": %" PRIu64 ",\n", s->prediction_hits);
    fprintf(out, "  \"prediction_misses\": %" PRIu64 ",\n", s->prediction_misses);
    fprintf(out, "  \"over_reservation_bytes\": %" PRIu64 ",\n", s->over_reservation_bytes);
    fprintf(out, "  \"wall_time_seconds\": %.6f,\n", s->wall_time_seconds);
    fprintf(out, "  \"cpu_time_seconds\": %.6f,\n", s->cpu_time_seconds);
    fprintf(out, "  \"tokens_per_second\": %.2f,\n", s->tokens_per_second);
    fprintf(out, "  \"bytes_per_second\": %.2f\n", s->bytes_per_second);
    fprintf(out, "}\n");
}

void report_write_csv_header(FILE *out) {
    if (!out) return;
    fprintf(out, "label,dataset,buffer_size,source_bytes,tokens,refills,spill_events,"
                 "spill_allocs,spill_reallocs,growth_copy_bytes,requested_capacity,peak_capacity,"
                 "pred_triggers,pred_hits,pred_misses,over_reservation_bytes,"
                 "wall_time_sec,cpu_time_sec,tok_per_sec,mb_per_sec\n");
}

void report_write_csv_row(FILE *out, const statistics_t *s, const char *label, 
                          const char *dataset_name, size_t buffer_size) {
    if (!out || !s) return;
    fprintf(out, "%s,%s,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
                 "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu,"
                 "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
                 "%.6f,%.6f,%.2f,%.2f\n",
            label ? label : "Run",
            dataset_name ? dataset_name : "default",
            buffer_size,
            s->total_source_bytes,
            s->total_tokens,
            s->input_refill_count,
            s->lexeme_spill_events,
            s->spill_allocations,
            s->spill_reallocations,
            s->growth_copy_bytes,
            s->total_requested_capacity,
            s->peak_spill_capacity,
            s->prediction_triggers,
            s->prediction_hits,
            s->prediction_misses,
            s->over_reservation_bytes,
            s->wall_time_seconds,
            s->cpu_time_seconds,
            s->tokens_per_second,
            s->bytes_per_second / (1024.0 * 1024.0));
}
