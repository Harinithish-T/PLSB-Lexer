# PLSB Testing Framework & Verification Suite

This document describes the automated test architecture, test matrix, failure-injection mechanisms, and verification invariants for the **PLSB (Predictive Lexeme Spill Buffering)** project.

---

## 1. Testing Philosophy & Guarantees

Every claim in the PLSB project is verified through executable automated tests:
1. **Zero External Test Dependencies**: Built using standard C11 assertions and explicit status counters for maximum portability across Windows (MinGW/Clang) and Linux (GCC/Clang).
2. **Deterministic Behavior**: All tests run synchronously with zero non-deterministic timing dependencies or flaky assertions.
3. **Equivalence Oracle Invariant**: The test suite mandates that the emitted token stream must match 100% between Baseline and PLSB under all supported buffer sizes ($B \ge 1$).

---

## 2. Test Suite Matrix (8 Comprehensive Suites)

### Suite 1: Unit Tests — Input Buffer (`tests/unit/test_input_buffer.c`)
- **Assertions**: 24 checks
- **Coverage**:
  - Empty input handling and immediate EOF transition without null dereference.
  - Single-byte buffer capacity ($B = 1$) stress-testing alternating refills.
  - Reassembled data integrity over multi-block streaming.
  - Embedded literal `\0` vs buffer boundary sentinel distinction (`(forward == buffer + bytes_read)`).

### Suite 2: Unit Tests — Predictor & Markov Modeling (`tests/unit/test_predictor.c`)
- **Assertions**: 9 checks
- **Coverage**:
  - EWMA smoothing precision under configurable $\alpha$.
  - Discrete histogram empirical $P_{75}$ computation.
  - Markov transition frequency tracking and probability normalization $\sum_j P(c_j \mid c_i) = 1$.
  - Safety clamping ($C_{min}$, $C_{max}$, and scanned prefix accommodation).

### Suite 3: Unit Tests — Spill Buffer (`tests/unit/test_spill_buffer.c`)
- **Assertions**: 20 checks
- **Coverage**:
  - Conventional baseline fixed initial capacity and geometric doubling ($16 \to 32 \to 64$).
  - Exact byte tracking of growth copying ($\text{growth\_copy\_bytes} = \sum L_{old}$).
  - PLSB prediction hits avoiding reallocations.
  - Over-reservation byte calculation for prediction hits.

### Suite 4: Unit Tests — Lexer Rules & Syntax (`tests/unit/test_lexer.c`)
- **Assertions**: 25 checks
- **Coverage**:
  - Identification of all 35 keywords vs general identifiers.
  - Literal formats: integers (dec, hex, oct, bin), floats (exponents, suffixes).
  - String and character literal escape sequence preservation (`\"`, `\\`, `\n`, `\t`).
  - Line and column position accuracy.
  - Error classification for unterminated tokens.

### Suite 5: Integration Tests — Boundary Conditions (`tests/integration/test_boundary_conditions.c`)
- **Assertions**: 25 checks
- **Coverage**:
  - **Buffer capacity matrix**: Evaluates token stream equivalence across capacities $B \in \{1, 2, 3, 7, 16, 31, 32, 64, 256, 1024\}$.
  - Tokens spanning multiple buffer refills: 1,500-character identifiers spanning 16-byte buffers.
  - Boundary split operators (`==`, `<<=`, `->`) split across 1-byte buffers.
  - Boundary split comment delimiters (`/*`, `*/`) split across 2-byte buffers.
  - CRLF (`\r\n`) line ending tracking.

### Suite 6: Integration Tests — Equivalence Verification (`tests/integration/test_comparison.c`)
- **Assertions**: 16 checks
- **Coverage**:
  - Direct Oracle verification comparing Conventional Baseline and PLSB over diverse buffer sizes, asserting that every token matches identically in type, lexeme text, length, line, and column.

### Suite 7: Regression & Hardening Suite (`tests/integration/test_regression.c`)
- **Test Cases**: 11 distinct edge-case suites
- **Assertions**: 1,739 checks
- **Coverage**:
  - **Case 1**: Reported cross-buffer lookahead defect for input `x ..a` at buffer size 4, yielding exact tokens `x`, `.`, `.`, `a`.
  - **Case 2**: Dot sequences matrix: `.`, `..`, `...`, `....`, `..a` tested across buffer sizes 1..16 and varying leading offsets.
  - **Case 3**: Multi-character operators (`==`, `!=`, `<=`, `>=`, `++`, `--`, `->`, `+=`, `-=`, `*=` `<<=`, `>>=`) matching and non-matching across boundaries.
  - **Case 4**: Non-matching operator lookaheads (e.g. `=x`, `+y`, `<z`) pushing back characters across buffer boundaries.
  - **Case 5**: EOF immediately following single- and multi-character operators.
  - **Case 6**: Number literals with dots and ranges (`.5`, `123.456`, `1..5`, `1 .. 5`, `0..`).
  - **Case 7**: 500-character tokens across 4-byte buffers (125 refills) verifying data integrity.
  - **Case 8 (Allocation Failure Injection)**: Deterministic mock failing allocator verifying initial spill allocation failures, append expansion/realloc failures, and normal token allocation failures cleanly emit `TOKEN_ERROR` without leaks or crashes.
  - **Case 9 (Arithmetic Overflow Guards)**: `SIZE_MAX` checks in `spill_buffer_begin`, `spill_buffer_append`, and `input_buffer_init`.
  - **Case 10 (I/O Error Propagation)**: Peeking and reading errors during lookahead explicitly emit `TOKEN_ERROR` ("I/O read error") and clean up lexer state safely.
  - **Case 11 (Token Array Allocation Safety & Overflow)**: Hardened `token_array_t` verifying safe NULL guards, capacity arithmetic overflow rejection (`SIZE_MAX / sizeof(token_t)`), injected `realloc` failure preserving previous buffer pointers, and dynamic expansion across 20 tokens with leak-free cleanup.

### Suite 8: Fixture Tests & Master Runner (`tests/test_runner.c`)
- **Assertions**: 11 fixture checks + unified invocation of Suites 1–7
- **Coverage**:
  - Execution on real fixtures (`empty.c`, `whitespace_only.c`, `sample_c.c`, `stress_long_tokens.c`).
  - Verification that buffer refill counts remain identical between Baseline and PLSB.

---

## 3. Running the Comprehensive Test Suite

### Via PowerShell (Windows)
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test
```

### Via Make (Linux / macOS)
```bash
make test
```

### Direct Execution
```bash
./bin/test_runner.exe
```

**Expected Output:**
```
======================================================================
  PLSB LEXER - MASTER COMPREHENSIVE TEST SUITE RUNNER
======================================================================

[TEST] Running Unit Tests: Input Buffer...
  Results: 24/24 tests passed (0 failures)
[TEST] Running Unit Tests: Predictor & Markov Modeling...
  Results: 9/9 tests passed (0 failures)
[TEST] Running Unit Tests: Spill Buffer...
  Results: 20/20 tests passed (0 failures)
[TEST] Running Unit Tests: Lexer Rules & C Syntax...
  Results: 25/25 tests passed (0 failures)
[TEST] Running Integration Tests: Boundary Conditions & Buffer Refills...
  Results: 25/25 tests passed (0 failures)
[TEST] Running Integration Tests: Equivalence Verification (Baseline vs PLSB)...
  Results: 16/16 tests passed (0 failures)
======================================================================
  PLSB LEXER - PERMANENT REGRESSION & HARDENING TEST SUITE
======================================================================
  [CASE 1] Reported defect 1: 'x ..a' with buffer size 4... -> PASS
  [CASE 2] Dot sequences (., .., ..., ...., ..a) across buffer sizes 1..16... -> PASS
  [CASE 3] Multi-char operators matching and non-matching lookahead... -> PASS
  [CASE 4] Non-matching operator lookaheads across boundaries... -> PASS
  [CASE 5] EOF immediately after single and multi-character operators... -> PASS
  [CASE 6] Number literals with dots and ranges (.5, 123.456, 1..5, 0..)... -> PASS
  [CASE 7] Tokens spanning multiple buffer refills (length 500 in buffer size 4)... -> PASS
  [CASE 8] Injected allocation failures... -> PASS
  [CASE 9] Arithmetic overflow guards in spill_buffer and input_buffer... -> PASS
  [CASE 10] I/O error propagation and cleanup on lookahead/refills... -> PASS
  [CASE 11] Token array allocation failure safety, overflow guards, and leak-free cleanup... -> PASS
  Assertions: 1739 passed, 0 failed (Total 11 cases)
=== RUNNING FIXTURE TESTS ===
  Fixture Checks: 11 Passed, 0 Failed
======================================================================
MASTER TEST SUITE SUMMARY:
  Suites Status: ALL 8 SUITES PASSED (0 suites failed)
======================================================================
```
