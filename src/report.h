#ifndef PLSB_REPORT_H
#define PLSB_REPORT_H

#include <stdio.h>
#include "statistics.h"

#ifdef __cplusplus
extern "C" {
#endif

void report_print_summary(FILE *out, const statistics_t *stats, const char *label);

void report_print_comparison(FILE *out, const statistics_t *baseline, const statistics_t *plsb);

void report_write_json(FILE *out, const statistics_t *stats, const char *label);

void report_write_csv_header(FILE *out);

void report_write_csv_row(FILE *out, const statistics_t *stats, const char *label, 
                          const char *dataset_name, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* PLSB_REPORT_H */
