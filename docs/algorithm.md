# PLSB Algorithm Specification

## 1. Mathematical Formulation

Let $\mathcal{C} = \{c_0, c_1, \dots, c_{K-1}\}$ denote the finite set of token classes:
$$\mathcal{C} = \{\text{Identifier}, \text{Number}, \text{String}, \text{Char}, \text{Operator}, \text{Punctuation}, \text{Comment}, \text{Other}\}$$
where $K = 8$.

### Online Length Estimation

For each token class $c \in \mathcal{C}$, the predictor maintains online length observations upon the completion of valid tokens:

1. **Exponentially Weighted Moving Average (EWMA)**:
   Given smoothing factor $\alpha \in (0, 1]$ (default $\alpha = 0.20$):
   $$\text{EWMA}_t(c) = \alpha \cdot L_t + (1 - \alpha) \cdot \text{EWMA}_{t-1}(c)$$
   with $\text{EWMA}_1(c) = L_1$.

2. **Empirical 75th Percentile ($P_{75}$)**:
   Maintained using a discrete frequency histogram $H_c[l]$ for $l \in [0, L_{max}-1]$ (where $L_{max} = 4096$):
   $$P_{75}(c) = \min \left\{ k \;\middle|\; \sum_{i=0}^k H_c[i] \ge \lceil 0.75 \cdot N_c \rceil \right\}$$
   where $N_c$ is the total observation count for class $c$. If $N_c < N_{min}$ (default $N_{min} = 5$), $P_{75}$ is considered unestablished and falls back to EWMA or safe minimum capacity.

3. **Markov Transition Context**:
   The transition count matrix $T[i][j]$ records transitions from previous token class $c_i$ to current token class $c_j$:
   $$P(c_j \mid c_i) = \frac{T[i][j]}{\sum_{k=0}^{K-1} T[i][k]}$$
   When context conditioning is active, the estimator uses conditional statistics $\text{EWMA}(c_i \to c_j)$ conditioned on the preceding token class.

## 2. Capacity Prediction Rules

When a token of candidate class $c$ crosses a buffer boundary and requires spill allocation, initial capacity is computed from signals that already exist at that instant. No future tokens are inspected.

### Production rule: Combined PLSB

Let $L$ be the number of bytes already scanned for the current lexeme.

$$
\begin{aligned}
Q_t &= \begin{cases} P_{75}(c) & \text{if } N_c \ge N_{min} \\ 0 & \text{otherwise} \end{cases} \\
E_t &= \begin{cases} \lceil \text{EWMA}(c) \rceil & \text{if } N_c > 0 \\ 0 & \text{otherwise} \end{cases} \\
M_t &= \begin{cases} \lceil \text{EWMA}(c_{prev} \to c) \rceil & \text{if } N(c_{prev} \to c) > 0 \\ 0 & \text{otherwise} \end{cases} \\
C_{\mathrm{raw}} &= \max(Q_t, E_t, M_t) \quad\text{(or } C_{safe\_min}\text{ if that max is 0)} \\
C_t &= \operatorname{clamp}\big(\max(C_{\mathrm{raw}}, L+1),\; C_{min},\; C_{max}\big)
\end{aligned}
$$

- $Q_t$ is a robust class-length percentile (needs at least $N_{min}$ finished tokens of class $c$).
- $E_t$ tracks recent class lengths (EWMA, default $\alpha = 0.20$).
- $M_t$ is a **length** estimate for the observed previous-class $\to$ current-class transition. It is not a next-token-type prediction. If that transition has never been seen, $M_t = 0$ and does not contribute.
- $C_{min} = C_{safe\_min}$ (default 16 B), $C_{max}$ default 4096 B.

This is the only estimator used by `SPILL_POLICY_PLSB` unless an ablation `--rule` is requested.

### Ablation rules (research only, not shown as competing PLSB products)

1. **Baseline**: $C_{pred} = C_{baseline}$ (16, 32, or 64 bytes).
2. **$P_{75}$-Only**: $P_{75}(c)$ or $C_{safe\_min}$ if $N_c < N_{min}$.
3. **EWMA-Only**: $\lceil \text{EWMA}(c) \rceil$ or $C_{safe\_min}$.
4. **Max ($P_{75}$, EWMA)**: $\max(P_{75}(c), \lceil \text{EWMA}(c) \rceil)$.
5. **Weighted**: $\lceil w \cdot P_{75}(c) + (1-w)\cdot\text{EWMA}(c) \rceil$ (default $w = 0.70$).
6. **Markov-Conditioned length**: $\lceil \text{EWMA}(c_{prev} \to c) \rceil$ when that transition exists, otherwise $\max(P_{75}, \text{EWMA})$.

### Safety Bounds Clamping

To guarantee memory safety and correctness under all conditions:
$$C_{init} = \max\Big(C_{safe\_min},\; \min\big(C_{pred},\; C_{max}\big),\; L_{scanned} + 1\Big)$$
where:
- $C_{safe\_min}$ is the minimum reservation floor (default 16 bytes).
- $C_{max}$ is the upper reservation ceiling (default 4096 bytes) to prevent outlier bloat.
- $L_{scanned}$ is the number of prefix bytes already accumulated in the retired buffer before the spill event triggered.

## 3. Geometric Growth Fallback

If the actual lexeme length exceeds $C_{init}$, the spill buffer expands using safe geometric doubling:
$$C_{new} = 2 \cdot C_{current} \quad \text{while } C_{new} < L_{needed}$$
Each reallocation incurs:
- A reallocation counter increment ($+1$).
- Data movement overhead: $\text{growth\_copy\_bytes} \mathrel{+}= L_{current}$.

## 4. Formal Metric Definitions

- **Refill Count**: Number of times `fread` or string refills occurred on either buffer.
- **Lexeme Spill Event**: A token that crossed at least one input-buffer boundary and required dynamic preservation.
- **Initial Spill Allocation**: Initial heap allocation for a spilled token.
- **Spill Reallocation**: Subsequent heap resizing when initial capacity was exceeded.
- **Growth-Copy Bytes**: Cumulative bytes copied strictly due to buffer resizing:
  $$\text{CopyBytes} = \sum_{\text{reallocations}} L_{old}$$
- **Prediction Hit**: A spilled token whose complete lexeme fit within the predicted initial capacity with 0 reallocations.
- **Prediction Miss**: A spilled token requiring one or more geometric reallocations.
- **Over-Reservation Bytes**: For prediction hits, excess bytes allocated beyond actual requirement:
  $$\text{OverReservation} = C_{init} - (L_{actual} + 1)$$
