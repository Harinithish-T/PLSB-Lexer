# Benchmark Datasets & Provenance Documentation

This document records the official benchmark suite fixtures, dataset characteristics, and provenance for the **PLSB (Predictive Lexeme Spill Buffering)** experimental evaluation.

## 1. Corpus Inventory & Characteristics

| Dataset File | Category | Size (Bytes) | Key Lexical Characteristics | Primary Experimental Purpose |
|:---|:---|:---|:---|:---|
| `tests/fixtures/sample_c.c` | Standard C Program | ~936 B | Balanced keywords, identifiers, math operators, loops. | Baseline compiler steady-state; tests standard token distribution. |
| `tests/fixtures/stress_long_tokens.c` | Boundary Stress | ~1,406 B | Extended block comments (>400B), long strings (>300B), long identifiers (>100B). | Severe boundary crossing stress; tests geometric reallocation cascades. |
| `tests/fixtures/long_identifiers.c` | Enterprise / Codegen | ~2,100 B | Symbol names 32–96 characters, AST descriptors, camelCase & snake_case. | Evaluates PLSB P75 & EWMA prediction for identifier spill buffering. |
| `tests/fixtures/long_comments.c` | Documentation Heavy | ~2,600 B | Multi-paragraph license terms, Dragon Book architectural notes (>1KB comments). | Tests long comment spill buffering and P75 convergence over comment bursts. |
| `tests/fixtures/long_strings.c` | Embedded Literals | ~1,600 B | Embedded JSON schema (>600B string) and SQL table DDL (>400B string). | Evaluates string literal spill prediction and copy byte reduction. |
| `benchmarks/datasets/realworld_c_sample.c` | Systems C | ~3,500 B | Virtual machine dispatcher, opcodes, stack management, switch-case, bitmasks. | Realistic systems software distribution representative of real compilers/interpreters. |

---

## 2. Dataset Provenance & Reproducibility Policy

1. **Deterministic Execution**: All datasets are committed directly to the repository and are free of external network or dynamic dependencies.
2. **Equivalence Oracle Verification**: Every benchmark run executes an automated pre-run equivalence oracle outside the timed loop comparing the token stream against the baseline token stream token-by-token. If any token type, lexeme, line, or column deviates, the run is aborted.
3. **Buffer Matrix Scaling**: Datasets are evaluated across the full input buffer size spectrum ($B \in \{16, 32, 64, 256, 1024, 4096, 16384\}$ bytes):
   - At $B = 16$ and $B = 32$: Spill events occur frequently, making allocation and copy byte behavior prominent.
   - At $B = 4096$ and $B = 16384$: Spills are rare or absent, isolating the runtime overhead of online statistics tracking and prediction.
