/*
 * STRESS TEST FIXTURE: Long tokens, multi-line comments, boundary crossers.
 * This file is specifically crafted to stress-test lexeme spill buffering,
 * growth reallocations, and Markov transitions.
 */

/* Very long comment block:
 * Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor
 * incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud
 * exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure
 * dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur.
 * Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit.
 */

static const char *kLongStringConstant = 
    "This is an unusually extended string literal meant to span across many small input buffer boundaries. "
    "It tests whether the spill buffer preserves every single character without dropping or duplicating bytes. "
    "Special escape sequences are included: \t\r\n\\\"quote\\\" and hex \\x41\\x42\\x43.";

// A very long identifier:
int var_with_an_exceptionally_long_descriptive_name_that_exceeds_typical_small_buffer_capacities_and_requires_spill_storage = 123456789;

double complex_scientific_constant = 1.2345678901234567e-10;

int test_operators(int a, int b) {
    if ((a <<= 2) >= (b >>= 1) && (a != b || a == 0)) {
        return a + b;
    }
    return a - b;
}
