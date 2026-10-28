#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Structure declaration */
typedef struct {
    int id;
    double score;
    char name[64];
} record_t;

// Global counter
static unsigned int g_counter = 0;

int calculate_metrics(const record_t *rec, int multiplier) {
    if (rec == NULL || multiplier <= 0) {
        return -1;
    }
    
    int result = rec->id * multiplier;
    if (result > 1000) {
        printf("Record %s exceeded threshold: %d\n", rec->name, result);
        result >>= 1;
    } else {
        result += 42;
    }
    
    /* Bitwise and logical manipulation */
    result = (result ^ 0xFF) | 0x01;
    return result;
}

int main(int argc, char **argv) {
    // Initialize record
    record_t item;
    item.id = 101;
    item.score = 98.75;
    strcpy(item.name, "SampleRecord_Alpha");
    
    int metric = calculate_metrics(&item, 5);
    printf("Final metric: %d\n", metric);
    return 0;
}
