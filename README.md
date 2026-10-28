# Predictive Lexical Analysis Buffering (PLSB)

> **Research Prototype**: Program Statistics, Token Frequency, and Markov-Conditioned Capacity Allocation for Streaming Lexical Input Buffering.

[![C11 Standard](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-brightgreen.svg)]()
[![Tests](https://img.shields.io/badge/Tests-8%20Suites%20Passed-success.svg)]()
[![Equivalence Oracle](https://img.shields.io/badge/Semantic%20Equivalence-100%25%20Verified-blueviolet.svg)]()

---

## 1. Research Overview & Hypothesis

In streaming compiler frontends, lexical analyzers rely on fixed-size alternating input buffers (the Dragon Book $2 \times B$ scheme) to process arbitrarily large files incrementally. When a lexeme crosses a buffer boundary, its prefix must be preserved in a dynamic *spill buffer* to prevent overwrite upon subsequent refills.

**Conventional Baseline approach:** Allocates a small fixed initial capacity ($C_0 = 16$ or $32$ bytes) and doubles geometrically ($16 \to 32 \to 64 \to 128 \to \dots$). On long tokens (e.g., wide block comments, multi-line string schemas, generated AST identifiers), this policy incurs repeated heap reallocations, repeatedly copying previously scanned bytes.

**Predictive Lexeme Spill Buffering (PLSB)** evaluates the hypothesis:
> *Using per-token-class length statistics, an online EWMA estimator, empirical percentiles ($P_{75}$), and optional preceding-token Markov context to select initial spill-buffer capacity can reduce repeated reallocations and growth-copy overhead, while preserving 100% identical tokenization behavior and buffer refill mechanics.*

---

## 2. Key Experimental Findings

Full experiment matrix: **336 benchmark configurations** — 6 datasets × 8 policies × 7 buffer sizes × 3 iterations.
All 336 configurations passed the automated Equivalence Oracle (100% token-stream identity).
Results source: `results/benchmark_latest.json` (2026-10-11, matrix_mode: full).

**Mean reallocations across all 6 datasets — Baseline (16B init) vs PLSB (Max P75+EWMA):**

| Input Buffer Size ($B$) | Baseline (16B) Reallocs | PLSB (Max) Reallocs | Reduction | Copy Bytes Reduction |
|:---|:---|:---|:---|:---|
| **$B = 16\text{ B}$** | 37.2 | 16.2 | **−56.5%** | −24.7% |
| **$B = 32\text{ B}$** | 22.0 | 11.5 | **−47.7%** | −13.6% |
| **$B = 64\text{ B}$** | 14.2 | 7.8 | **−44.7%** | −17.1% |
| **$B = 256\text{ B}$** | 4.3 | 2.5 | **−42.3%** | −10.6% |
| **$B = 1024\text{ B}$** | 1.5 | 0.7 | **−55.6%** | −10.7% |
| **$B = 4096\text{ B}$** | 0.0 | 0.0 | No spills | No spills |
| **$B = 16,384\text{ B}$** | 0.0 | 0.0 | No spills | No spills |

**Dataset-specific observations at $B = 32\text{ B}$:**

| Dataset | Baseline (16B) | PLSB (Max) | Notes |
|:---|:---|:---|:---|
| `long_strings.c` | 37.0 reallocs, 1067B copy | 10.0, 539B | **−73% reallocs, −49% copy** |
| `long_comments.c` | 14.0 reallocs, 4543B copy | 7.0, 4096B | **−50% reallocs, −9.8% copy** |
| `long_identifiers.c` | 40.0 reallocs, 1037B copy | 21.0, 841B | −47.5% reallocs, −18.9% copy |
| `sample_c.c` | 6.0 reallocs, 59B copy | 4.0, 47B | −33% reallocs, −20% copy |
| `realworld_c_sample.c` | 16.0 reallocs, 618B copy | 14.0, 571B | −12.5% reallocs |
| `stress_long_tokens.c` | 19.0 reallocs, 931B copy | 13.0, 1038B | −31.6% reallocs; **+11.5% copy (regression)** |

**Honest trade-off disclosures:**
- `stress_long_tokens.c` at $B = 32\text{ B}$: PLSB copy bytes are **higher** (+11.5%) than baseline — the predictor over-reserves on variable-length tokens.
- At $B \ge 4096\text{ B}$: zero spill events; PLSB predictor overhead adds measurable CPU time with no allocation benefit.
- EWMA-Only at $B = 32\text{ B}$ for `long_identifiers.c`: 39.0 reallocs vs baseline 40.0 — no benefit.
- Best-performing policy varies by dataset; **PLSB (Max P75+EWMA)** is the most consistent winner across the corpus.

**Verification guarantee:**
- **Zero Semantic Divergence:** Equivalence Oracle verified all 336 configurations — identical token types, line numbers, column positions, lengths, and lexemes.
- Regression suite: **11 cases, 1,739 assertions, all passing** (including allocation-failure injection, token-array safety, and I/O error propagation).

---

## 3. Directory Layout

```
c:\PLSB_Lexer\
├── Makefile                     # Standard POSIX / GCC Makefile
├── build.ps1                    # Native Windows PowerShell build & test automation
├── README.md                    # Project documentation & master specification
├── docs/
│   ├── architecture.md          # Complete double-buffer & spill-buffer system architecture
│   ├── algorithm.md             # Mathematical specifications of P75, EWMA & Markov rules
│   ├── benchmark_methodology.md # Scientific measurement rigor, timer invariants & oracle
│   ├── testing.md               # Unit, integration, and regression test suite specifications
│   ├── research_paper_draft.md  # Comprehensive research paper draft for submission
│   └── faculty_presentation_guide.md # Faculty / examiner walkthrough and defense script
├── src/
│   ├── main.c                   # CLI driver with text, JSON, and CSV reporting
│   ├── input_buffer.[c|h]       # Two-buffer alternating engine with boundary sentinels
│   ├── lexer.[c|h]              # Full C-like scanner with lookahead queue & error guards
│   ├── spill_buffer.[c|h]       # Dynamic spill manager with geometric growth & copy tracking
│   ├── predictor.[c|h]          # Online P75, EWMA, and Markov transition matrix engine
│   ├── statistics.[c|h]         # Unified performance metrics and timing instrumentation
│   └── report.[c|h]             # Formatted reports, JSON serialization, and CSV export
├── tests/
│   ├── unit/                    # Isolated unit test suites (input buffer, spill buffer, predictor, lexer)
│   ├── integration/             # Integration suites (boundary conditions, comparison, regression)
│   ├── fixtures/                # Deterministic benchmark fixtures (sample_c, stress, long tokens)
│   └── test_runner.c            # Master comprehensive test runner executing all 8 suites
├── benchmarks/
│   ├── benchmark_runner.c       # Multi-run statistical benchmark engine with Equivalence Oracle
│   ├── datasets.md              # Corpus inventory and provenance documentation
│   └── datasets/                # Extended real-world corpus files (realworld_c_sample.c)
├── dashboard/
│   ├── server.py                # Zero-dependency Python HTTP server (API & static files)
│   ├── index.html               # 11-panel interactive presentation dashboard
│   ├── styles.css               # Modern dark/light academic theme with presentation mode
│   └── app.js                   # Live interactive Chart.js visualizations & token diff inspector
└── results/
    ├── benchmark_latest.json    # Full structured JSON matrix of benchmark results
    ├── benchmark_summary.csv    # Aggregated mean/stddev/median CSV metrics
    └── benchmark_raw.csv        # Per-iteration raw observation logs
```

---

## 4. Build and Test Instructions

### Windows (MinGW-w64 / GCC via PowerShell)
```powershell
# 1. Clean build and run comprehensive master test suite:
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test

# 2. Run reproducible scientific benchmark runner:
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Benchmark
```

### Linux / macOS (GCC / Make)
```bash
# 1. Compile all binaries and run all test suites:
make test

# 2. Run benchmark matrix:
make benchmark
```

---

## 5. Running the Interactive Presentation Dashboard

The dashboard provides a complete interactive visual environment designed for faculty evaluations, live demonstrations, and paper artifact review.

```bash
# Start the zero-dependency Python dashboard server on port 8080:
python dashboard/server.py 8080
```

Open your browser to: **`http://localhost:8080/`**

### Available Dashboard Features:
1. **Interactive Architecture Diagram:** SVG visualizer tracing tokens from the input stream to buffers, lookahead queues, spill buffer, and predictor.
2. **Dataset Management & Source Viewer:** One-click loading of all 6 fixtures with source code preview and custom code editor.
3. **Live Hyperparameter Tuning:** Real-time sliders for EWMA smoothing ($\alpha$), P75 weight, buffer size ($16\text{ B}$–$16,384\text{ B}$), and prediction rules.
4. **Live C Engine Execution:** Real-time subprocess execution of `plsb_lexer` with live terminal streaming.
5. **Side-by-Side Comparison Table:** Immediate metrics with percentage improvement badges ($\Delta\%$).
6. **Interactive Visualizations:** Canvas & Chart.js charts showing reallocations by buffer size, copy bytes moved, and throughput.
7. **Token Stream Inspector:** Visual token explorer with `[SPILLED]` status badges and side-by-side semantic equivalence verification.
8. **Markov Transition Matrix Heatmap:** Heatmap of transition probabilities $P(C_t \mid C_{t-1})$.
9. **Presentation Mode:** Full-screen slide-optimized layout for examiner defense.

---

## 6. CLI Driver Usage

```bash
# Compare Baseline and PLSB with side-by-side verification:
./bin/plsb_lexer --mode compare --buffer-size 32 tests/fixtures/stress_long_tokens.c

# Dump emitted tokens with spill status:
./bin/plsb_lexer --mode plsb --buffer-size 32 --rule max --dump-tokens tests/fixtures/sample_c.c

# Run benchmark runner with CLI options:
./bin/benchmark_runner --dataset tests/fixtures/stress_long_tokens.c --quick
./bin/benchmark_runner --all-datasets --full
```

---

## 7. License

Academic Research & MIT Open Source License.
