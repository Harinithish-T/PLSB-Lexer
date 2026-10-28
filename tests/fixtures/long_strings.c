/* Benchmark Fixture: long_strings.c
 * Characteristics: Extended string literals containing embedded JSON schemas,
 * multi-line SQL definitions, and escape sequences crossing input buffer boundaries.
 */

#include <stdio.h>

static const char *const g_json_schema_definition = 
    "{\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",\"title\":\"CompilerMetricsReport\","
    "\"type\":\"object\",\"properties\":{\"benchmark_run_id\":{\"type\":\"string\",\"description\":\"Unique UUID for experiment execution\"},"
    "\"timestamp_epoch_ms\":{\"type\":\"integer\",\"description\":\"Epoch millisecond measurement marker\"},"
    "\"dataset_provenance\":{\"type\":\"string\",\"description\":\"Source file path and integrity checksum SHA256\"},"
    "\"metrics\":{\"type\":\"object\",\"properties\":{\"lexeme_spill_events\":{\"type\":\"integer\"},"
    "\"spill_reallocations\":{\"type\":\"integer\"},\"growth_copy_bytes\":{\"type\":\"integer\"},"
    "\"peak_spill_capacity\":{\"type\":\"integer\"},\"over_reservation_bytes\":{\"type\":\"integer\"},"
    "\"prediction_hits\":{\"type\":\"integer\"},\"prediction_misses\":{\"type\":\"integer\"},"
    "\"wall_clock_nanoseconds\":{\"type\":\"integer\"}},\"required\":[\"lexeme_spill_events\",\"spill_reallocations\"]}},"
    "\"status\":{\"type\":\"string\",\"enum\":[\"SUCCESS\",\"VERIFICATION_MISMATCH\",\"ALLOCATION_FAILURE\"]}}";

static const char *const g_sql_table_schema = 
    "CREATE TABLE IF NOT EXISTS benchmark_observations_v2 ("
    "  observation_id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  dataset_name TEXT NOT NULL,"
    "  buffer_size_bytes INTEGER NOT NULL,"
    "  policy_name TEXT NOT NULL,"
    "  iteration_index INTEGER NOT NULL,"
    "  tokens_scanned INTEGER NOT NULL,"
    "  source_bytes_processed INTEGER NOT NULL,"
    "  spill_events INTEGER NOT NULL,"
    "  reallocations INTEGER NOT NULL,"
    "  growth_copy_bytes INTEGER NOT NULL,"
    "  peak_capacity_bytes INTEGER NOT NULL,"
    "  over_reservation_bytes INTEGER NOT NULL,"
    "  execution_wall_time_ms REAL NOT NULL,"
    "  throughput_tokens_per_sec REAL NOT NULL"
    ");";

int print_schemas(void) {
    printf("%s\n\n%s\n", g_json_schema_definition, g_sql_table_schema);
    return 0;
}
