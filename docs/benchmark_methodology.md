# Benchmark Methodology & Scientific Rigor

This document details the experimental methodology, measurement protocol, statistical metrics, and fairness controls governing the **Predictive Lexeme Spill Buffering (PLSB)** research evaluation.

---

## 1. Principles of Fair Evaluation

To evaluate PLSB scientifically without bias:

1. **Shared Codebase & Single-Path Execution**:
   Baseline and PLSB execute through the exact same lexer DFA, token finalization, and statistics instrumentation code paths. The only operational difference is the computation of initial spill capacity ($C_{init}$).
2. **Pre-Run Equivalence Oracle**:
   Before executing the timed loop for any configuration matrix cell, an automated Equivalence Oracle executes outside the timed region. It performs a complete lexing pass with both Baseline and the candidate policy, verifying token-by-token identity:
   $$\text{Token}_i^{\text{candidate}} \equiv \text{Token}_i^{\text{baseline}} \iff (\text{type}, \text{line}, \text{column}, \text{length}, \text{lexeme})_{\text{cand}} = (\text{type}, \text{line}, \text{column}, \text{length}, \text{lexeme})_{\text{base}}$$
   If any difference occurs, the benchmark immediately aborts with an error. No speedup or allocation reduction is accepted without verified semantic equivalence.
3. **Timed Region Boundaries**:
   File opening, disk seeking, memory buffer allocation outside tokenization, oracle verification, and report serialization are strictly excluded from the timed measurement block. Only the loop consuming characters and emitting tokens is clocked.
4. **High-Resolution Monotonic Timers**:
   - **Windows**: `QueryPerformanceCounter` and `QueryPerformanceFrequency` with sub-microsecond precision.
   - **Linux / POSIX**: `clock_gettime(CLOCK_MONOTONIC, ...)` with `#define _POSIX_C_SOURCE 199309L`.
5. **No Buffer Refill Distortion**:
   Input buffer refills and total bytes read are logged and verified to remain identical across all policies.

---

## 2. Multi-Run Statistical Aggregation

To avoid reporting single-run fluctuations or cold-cache anomalies, every benchmark matrix cell is executed across $N$ independent iterations (quick $N = 3$, full $N = 3$; can be increased with `--iterations N`). The current saved full matrix (`results/benchmark_latest.json`, 336 configurations) used $N = 3$.

For each observed metric $X \in \{T_{\text{wall}}, \text{Reallocations}, \text{CopyBytes}, \text{Throughput}\}$:

1. **Sample Mean**:
   $$\bar{x} = \frac{1}{N} \sum_{i=1}^N x_i$$
2. **Sample Standard Deviation** ($N > 1$):
   $$s = \sqrt{\frac{1}{N - 1} \sum_{i=1}^N (x_i - \bar{x})^2}$$
3. **Sample Median**:
   $$\text{Median} = \begin{cases} x_{(k)} & \text{if } N = 2k - 1 \\ \frac{x_{(k)} + x_{(k+1)}}{2} & \text{if } N = 2k \end{cases}$$
4. **Extrema**:
   $$x_{\min} = \min_{1 \le i \le N} x_i, \quad x_{\max} = \max_{1 \le i \le N} x_i$$

---

## 3. Policy & Buffer Size Matrix

### Evaluated Policies (8 Configurations)
1. **Baseline (16B init)**: Standard fixed conservative initial capacity (16 bytes) with $2\times$ doubling.
2. **Baseline (32B init)**: Fixed initial capacity (32 bytes) with $2\times$ doubling.
3. **Baseline (64B init)**: Fixed initial capacity (64 bytes) with $2\times$ doubling.
4. **PLSB ($P_{75}$-Only)**: Initial capacity set to online 75th percentile of token class.
5. **PLSB (EWMA-Only)**: Initial capacity set to rounded EWMA length ($\alpha = 0.20$).
6. **PLSB (Max $P_{75}$ + EWMA)**: Conservative maximum $\max(P_{75}, \lceil\text{EWMA}\rceil)$.
7. **PLSB (Weighted 70/30)**: Convex combination $0.70 \cdot P_{75} + 0.30 \cdot \text{EWMA}$.
8. **PLSB (Markov-Conditioned)**: Context-conditioned length estimation based on preceding token class.

### Buffer Size Spectrum ($B$)
- **$B \in \{16, 32\}\text{ bytes}$**: Boundary stress regime. Lexemes frequently cross buffer boundaries, making spill reallocation and memory copying dominant.
- **$B \in \{64, 256\}\text{ bytes}$**: Intermediate embedded/constrained memory regime.
- **$B \in \{1024, 4096, 16384\}\text{ bytes}$**: Page-aligned desktop/server compiler regime. Boundary crossings are rare, isolating the raw computational overhead of online predictor statistics.

---

## 4. Benchmark Runner CLI Options

The C benchmark engine (`bin/benchmark_runner.exe`) supports complete command-line automation:

```bash
# Run specific dataset:
./bin/benchmark_runner --dataset tests/fixtures/stress_long_tokens.c

# Run all registered datasets in quick mode (3 buffer sizes, 3 iterations):
./bin/benchmark_runner --all-datasets --quick

# Run exhaustive matrix (all datasets, 7 buffer sizes, 10 iterations):
./bin/benchmark_runner --all-datasets --full

# Custom iterations and buffer size:
./bin/benchmark_runner --dataset tests/fixtures/sample_c.c --buffer-size 64 --iterations 10

# Override output artifact paths:
./bin/benchmark_runner --json results/my_run.json --raw-csv results/my_raw.csv --summary-csv results/my_sum.csv
```

Outputs are automatically exported in structured format:
- **`results/benchmark_latest.json`**: Machine-readable JSON matrix consumed directly by the interactive dashboard.
- **`results/benchmark_summary.csv`**: Aggregated mean, stddev, and median metrics.
- **`results/benchmark_raw.csv`**: Individual per-iteration trial logs.
