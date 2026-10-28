# Predictive Lexeme Spill Buffering: Adaptive Capacity Allocation for Streaming Lexical Analyzers

**Authors:** Compiler Systems Research Team  
**Artifact Repository:** `PLSB_Lexer` (C11 Implementation & Interactive Presentation Engine)  
**Keywords:** Compilers, Lexical Analysis, Streaming Input Buffering, Memory Management, Empirical Optimization

---

## Abstract

Lexical analyzers in industrial compilers process arbitrarily large input streams using an alternating two-buffer system ($2 \times B$ bytes), as formalized by Aho, Sethi, and Ullman. When a lexical item—such as an extended block comment, an embedded schema string, or an auto-generated identifier—spans beyond the current buffer into an alternating buffer, its prefix must be preserved in a heap-allocated *spill buffer* to prevent overwrite upon subsequent I/O refills. Traditional implementations allocate a fixed conservative initial capacity ($C_0 = 16$ or $32$ bytes) and rely on geometric doubling ($2\times$). On long lexemes, this policy induces cascading reallocations, repeatedly copying partial lexeme bytes across memory boundaries.

In this paper, we propose **Predictive Lexeme Spill Buffering (PLSB)**, an adaptive, online allocation policy that dynamically predicts initial spill-buffer capacity using empirical token-class length distributions (75th percentile $P_{75}$), exponentially weighted moving averages (EWMA), and optional preceding-token Markov context. We prove that PLSB introduces **zero semantic divergence**, preserving 100% identical token streams, line numbers, column coordinates, and lexemes. We evaluate PLSB across 336 benchmark configurations spanning 6 synthetic and realistic datasets, 8 allocation policies, 7 buffer sizes from $16\text{ B}$ to $16,384\text{ B}$, and 3 repetitions each. Under memory-constrained buffers ($B \le 256\text{ B}$), PLSB reduces spill-buffer reallocations by **42% to 57%** on average across the corpus, with dataset-specific improvements reaching up to **73%** for long string literals. We also disclose honest regressions: on the stress-long-tokens fixture at $B = 32\text{ B}$, PLSB's Max policy increases growth-copy bytes by 11.5% due to over-reservation. In large-buffer page regimes ($B \ge 4096\text{ B}$) where boundary crossings are nonexistent, online statistics tracking adds measurable CPU overhead with zero allocation benefit.

---

## 1. Introduction

Lexical analysis is the initial phase of every compilation pipeline. The lexer transforms an unstructured stream of ASCII or UTF-8 characters into a discrete sequence of tokens. Because source files can range from trivial scripts to multi-megabyte machine-generated files, compiling entire files into contiguous RAM is often infeasible in memory-constrained environments (such as embedded IDEs, real-time linters, or microcontroller toolchains).

To address this, the classic *Dragon Book* architecture employs a pair of fixed-size buffers, alternating refills via double buffering. This scheme operates in $O(1)$ auxiliary space for tokens that fit within a single buffer. However, when a token crosses a buffer boundary:
1. The prefix accumulated in the retiring buffer cannot be overwritten.
2. A separate dynamic spill buffer must be allocated to preserve the prefix across refills.
3. If the token continues for multiple refills (or exceeds initial spill capacity), the spill buffer must expand dynamically.

### The Allocation Penalty of Fixed Doubling
Conventional lexers initialize the spill buffer with a small constant $C_0$ (typically 16 bytes). If a block comment or long identifier reaches 500 bytes:
$$\text{Reallocations: } 16 \to 32 \to 64 \to 128 \to 256 \to 512 \quad (5 \text{ reallocations})$$
$$\text{Cumulative Bytes Copied: } 16 + 32 + 64 + 128 + 256 = 496 \text{ bytes}$$
This copying consumes memory bandwidth, pollutes CPU caches (L1/L2), and fragments the heap.

### The PLSB Hypothesis
Token lengths in structured programming languages are not uniformly distributed random variables; they exhibit strong class-dependent clustering:
- Keywords, operators, and punctuation are short and bounded ($\le 8$ characters).
- Normal identifiers typically range from 3 to 24 characters.
- Comments and string literals frequently exhibit heavy-tailed distributions spanning hundreds of bytes.

By dynamically tracking recent token lengths online, the lexer can estimate the probable final length of the candidate token at the exact moment a buffer boundary crossing occurs, allocating adequate capacity upfront.

---

## 2. Architecture & Buffer Boundary Invariants

```
                +----------------------------+
                |    Character Stream (I/O)  |
                +--------------+-------------+
                               | Chunks of B bytes
                               v
            +------------------+------------------+
            | Buffer 0 (B + 1) | Buffer 1 (B + 1) |
            | [Data ... '\0']  | [Data ... '\0']  |
            +------------------+------------------+
                               | Scan Pointer (forward)
                               v
            +------------------+------------------+
            |  Lookahead Queue & State Scanner    |
            |     - Safe cross-buffer peek(k)     |
            +------------------+------------------+
                               | Boundary Spill Event
                               v
            +------------------+------------------+
            |        Predictive Spill Manager     |
            |   Initial Cap = Predictor(Class)    |
            +------------------+------------------+
                               |
                               v
            +------------------+------------------+
            | Emitted Token Stream & Statistics   |
            +-------------------------------------+
```

### 2.1 Double-Buffering with Embedded Sentinels
Each buffer is sized to $B + 1$ bytes. The $(B+1)$-th byte stores a sentinel `\0`. When the scanner reaches `\0`, it evaluates:
$$\text{is\_boundary} \iff (\text{forward} == \text{buffer} + \text{bytes\_read})$$
This cleanly distinguishes a buffer boundary from an embedded literal `\0` in binary or test data.

### 2.2 Lookahead Queue Without Destructive Pushback
Multi-character operators (such as `...`, `==`, `->`, `<<=`) require up to 2 characters of lookahead. Earlier implementations relied on destructive pushback (`lexer_unget_char()`), which could corrupt state if characters crossed retiring buffer boundaries.

PLSB introduces a non-destructive lookahead abstraction:
$$\text{lexer\_peek}(l, k) \implies \text{inspects character at forward} + k$$
Characters residing across the boundary in the alternate buffer are inspected without moving the scan pointer or triggering premature spill allocations. Characters are consumed only when an operator or token prefix is positively accepted.

---

## 3. Online Predictive Sizing Formulation

Let $\mathcal{C} = \{\text{Identifier}, \text{Number}, \text{String}, \text{Char}, \text{Operator}, \text{Punctuation}, \text{Comment}, \text{Other}\}$ denote the 8 recognized token classes.

### 3.1 Exponentially Weighted Moving Average (EWMA)
For each token class $c \in \mathcal{C}$, the predictor maintains online length observations:
$$\text{EWMA}_t(c) = \alpha \cdot L_t + (1 - \alpha) \cdot \text{EWMA}_{t-1}(c)$$
where $\alpha \in [0.05, 0.95]$ (default $\alpha = 0.20$).

### 3.2 Discrete Histogram Empirical 75th Percentile ($P_{75}$)
To capture skewed token lengths without maintaining an unbounded sample window, PLSB uses a discrete frequency histogram $H_c[l]$ for lengths $l \in [0, 4096]$:
$$P_{75}(c) = \min \left\{ k \;\middle|\; \sum_{i=0}^k H_c[i] \ge \lceil 0.75 \cdot N_c \rceil \right\}$$
where $N_c$ is the total observation count for class $c$.

### 3.3 Markov Transition Context
PLSB tracks transitions between successive token classes:
$$P(C_t = c_j \mid C_{t-1} = c_i) = \frac{T[i][j]}{\sum_{k=0}^{K-1} T[i][k]}$$
When a spill event begins, the candidate token class is informed by the preceding token class $c_{prev}$.

### 3.4 Capacity Clamping & Safety Invariant
To guarantee that memory consumption remains bounded and that already-scanned characters fit safely:
$$C_{\text{init}} = \max\Big(C_{\text{safe\_min}},\; \min\big(C_{\text{pred}},\; C_{\text{max}}\big),\; L_{\text{scanned}} + 1\Big)$$
where $C_{\text{safe\_min}} = 16\text{ B}$ and $C_{\text{max}} = 4096\text{ B}$.

---

## 4. Verification & Correctness Guarantees

PLSB was hardened through an extensive multi-suite verification harness:
1. **Equivalence Oracle**: Every benchmark run enforces token-by-token comparison outside the timed loop:
   $$\text{Token}_i^{\text{PLSB}} \equiv \text{Token}_i^{\text{Baseline}}$$
2. **Allocation Failure Injection**: A mock failing allocator simulates out-of-memory events on initial allocation, expansion reallocations, and token finalization, verifying that `TOKEN_ERROR` is emitted without memory leaks or crashes.
3. **Arithmetic Overflow Guards**: All buffer calculations verify against `SIZE_MAX` before computation to prevent integer wrapping.
4. **Regression Hardening**: 11 distinct regression cases (1,739 assertions) verify dot sequences (`..`, `...`, `..a`), numbers (`1..5`), boundary-crossing lookahead, allocator injection, I/O error propagation, and token-array overflow guards.

---

## 5. Experimental Results

We evaluated 8 allocation policies across 6 benchmark datasets and 7 buffer sizes ($B \in \{16, 32, 64, 256, 1024, 4096, 16384\}$), with 3 repetitions per cell — **336 configurations total**. All 336 passed the Equivalence Oracle. Results are from `results/benchmark_latest.json` (2026-10-11, full matrix).

### 5.1 Reallocation and Memory Copy Reduction

**Mean reallocations across all 6 datasets — Baseline (16B init) vs PLSB (Max P75+EWMA):**

```
Buffer  | Baseline (16B) | PLSB (Max) | Realloc Reduction | Copy Bytes Reduction
--------|----------------|------------|-------------------|--------------------
  16 B  |      37.2      |    16.2    |      −56.5%       |       −24.7%
  32 B  |      22.0      |    11.5    |      −47.7%       |       −13.6%
  64 B  |      14.2      |     7.8    |      −44.7%       |       −17.1%
 256 B  |       4.3      |     2.5    |      −42.3%       |       −10.6%
1024 B  |       1.5      |     0.7    |      −55.6%       |       −10.7%
4096 B  |       0.0      |     0.0    |     No spills     |      No spills
16384 B |       0.0      |     0.0    |     No spills     |      No spills
```

**Per-dataset at B = 32 B (all policies):**

```
Dataset                 | base16 (R/C)  | base32 (R/C)  | max (R/C)     | Notes
------------------------|---------------|---------------|---------------|-------------------------------
long_strings.c          | 37 / 1067B    | 37 / 1250B    | 10 / 539B     | Max: −73% R, −49% copy
long_comments.c         | 14 / 4543B    | 13 / 4526B    |  7 / 4096B    | Max: −50% R, −9.8% copy
long_identifiers.c      | 40 / 1037B    | 46 / 1413B    | 21 / 841B     | Max: −47.5% R, −18.9% copy
sample_c.c              |  6 /   59B    |  2 /   28B    |  4 /   47B    | Max: −33% R, −20% copy
realworld_c_sample.c    | 16 /  618B    |  5 /  506B    | 14 /  571B    | Marginal gain; base32 better
stress_long_tokens.c    | 19 /  931B    | 15 /  872B    | 13 / 1038B    | REGRESSION: +11.5% copy bytes
```

**Key regressions that must be disclosed:**
- `stress_long_tokens.c`, $B = 32\text{ B}$: PLSB Max copy bytes = 1,038 B vs baseline 931 B — over-reservation due to short initial warm-up observations inflating the $P_{75}$ estimate.
- `long_identifiers.c`, EWMA-Only, $B = 32\text{ B}$: 39 reallocs — effectively no improvement over baseline 40.
- All datasets, $B \ge 4096\text{ B}$: PLSB predictor overhead measurable (0.08–0.12 ms vs 0.05–0.07 ms baseline) with zero allocation benefit.

**On `long_strings.c` ($B = 256\text{ B}$):**
- Baseline (16B): 7.0 reallocations, 271 copy bytes.
- PLSB (Max): 2.0 reallocations, 127 copy bytes.
- **−71.4% reallocations, −53.1% copy bytes.**

### 5.2 Throughput & Scalability Trade-Offs

When $B = 4096\text{ B}$ (where most files fit in a single buffer page):
- Spills: 0 for all datasets.
- Reallocations: 0 for all datasets.
- Baseline wall time: ~0.05–0.08 ms.
- PLSB Markov wall time: ~0.08–0.12 ms.

**Scientific Takeaway:** The predictive policy should be engaged dynamically: active in memory-constrained streaming modes ($B \le 512\text{ B}$), and may be bypassed when input buffer pages exceed typical maximum token sizes ($B \ge 4096\text{ B}$). At no buffer size does PLSB compromise tokenization correctness.

---

## 6. Conclusion

Predictive Lexeme Spill Buffering demonstrates that online empirical length estimation can eliminate 42%–57% of spill-buffer reallocations on average across a representative corpus, with up to 73% reduction on long string literal workloads. Dataset-specific regressions in growth-copy bytes (notably on stress-long-tokens at small buffers) highlight that over-reservation is a genuine risk for highly variable token distributions. By integrating strict non-destructive peeking, mathematical safety clamping, and automated equivalence verification, PLSB proves that dynamic memory optimization in compiler frontends can be achieved with zero semantic compromise. Future work should investigate adaptive policy switching based on observed $P_{75}$ variance, and per-class EWMA warm-up strategies to reduce early over-reservation.

