# PLSB Lexer Architecture

## Overview
Predictive Lexeme Spill Buffering (**PLSB**) is an experimental lexical analysis buffering subsystem designed for high-throughput streaming tokenizers. It manages input through an alternating two-buffer system with boundary sentinels and dynamically allocates spill buffers for tokens that span buffer boundaries.

## Subsystem Architecture

```
                    +-----------------------------+
                    |        Source Input         |
                    |   (File / Stream / Memory)  |
                    +--------------+--------------+
                                   | Incremental Refill (fread)
                                   v
             +---------------------+---------------------+
             |           Two-Buffer Input System         |
             |  +-------------------+ +---------------+  |
             |  | Buffer 0 (B + 1)  | | Buffer 1(B+1) |  |
             |  | [data ... '\0']   | | [data ...'\0']|  |
             |  +-------------------+ +---------------+  |
             +---------------------+---------------------+
                                   | Scan Pointer (forward)
                                   v
             +---------------------+---------------------+
             |         Streaming Token Scanner           |
             |   - Lexical states (C-like language)      |
             |   - Boundary detection via sentinel check |
             |   - Position tracking (line, column)      |
             +---------------------+---------------------+
                                   | Boundary Crossing Event
                    +--------------+--------------+
                    |                             |
                    v                             v
     +--------------------------+  +-------------------------------+
     | Conventional Baseline    |  | PLSB Predictive Policy        |
     | - Fixed initial capacity |  | - Per-class EWMA & P75        |
     | - Geometric 2x growth    |  | - Markov transition context   |
     +--------------+-----------+  | - Adaptive initial reservation|
                    |              +---------------+---------------+
                    \_____________________________/
                                   |
                                   v
             +---------------------+---------------------+
             |           Dynamic Spill Buffer            |
             |   - Preserves complete lexeme data        |
             |   - Reallocates geometrically if needed   |
             |   - Tracks growth copies & peak memory    |
             +---------------------+---------------------+
                                   | Token Finalization
                                   v
             +---------------------+---------------------+
             | Emitted Token Stream & Statistics Engine  |
             |   - Identical tokens across both policies |
             |   - JSON, CSV, and summary reports        |
             +-------------------------------------------+
```

## Detailed Component Responsibilities

### 1. Input Source & Two-Buffer System (`input_buffer.h`, `input_buffer.c`)
- **Incremental I/O**: Source data is read in chunks of $B$ bytes into alternating buffers `buffer[0]` and `buffer[1]`. Files are never loaded entirely into memory.
- **Sentinel Mechanics**:
  - Each buffer is allocated with capacity $B + 1$.
  - Upon refill, a sentinel NUL character (`\0`) is placed immediately after the valid loaded bytes at index `bytes_read`.
  - When the scanner reaches `\0`, it checks whether the pointer equals the buffer end boundary `buffer + bytes_read`.
  - If equal, it is a buffer boundary sentinel: the lexer either halts on genuine EOF or switches active buffers and refills the alternate buffer.
  - If not equal, it is an embedded literal `\0` character inside the source stream.
- **Position & Refill Integrity**:
  - Buffer refills never overwrite data that is currently being scanned.
  - Buffer refill counts and total bytes read are recorded identically regardless of allocation policy.

### 2. Lexical Scanner (`lexer.h`, `lexer.c`)
- Implements a C-like lexical language:
  - Identifiers and 35 standard C keywords (binary-search classified).
  - Numeric literals: decimal, hexadecimal (`0x`), octal (`0`), binary (`0b`), floating point (fractions, exponents `1e5`, suffixes `u`, `l`, `f`).
  - String and character literals: complete escape sequence support (`\"`, `\\`, `\n`, `\t`, `\r`, hex escapes).
  - Multi-character operators: `==`, `!=`, `<=`, `>=`, `&&`, `||`, `++`, `--`, `<<=`, `>>=`, `->`, `##`, `...`.
  - Comments: single-line (`// ...`) and multi-line (`/* ... */`).
  - Source position tracking: 1-based line and column numbers, correctly handling Windows `\r\n` and Unix `\n`.
- **Non-Destructive Boundary Lookahead Architecture**:
  - Replaces legacy destructive pushback (`lexer_unget_char()`) with `lexer_peek(l, k)` and `lexer_consume(l)`.
  - `lexer_peek(l, k)` inspects characters at offset $k$ ($0$-based) across both the active buffer and the alternate buffer without moving `forward` or triggering premature spill events.
  - Characters are consumed via `lexer_consume(l)` only after tokens or operator prefixes are positively accepted.
  - Safe error propagation: I/O read errors emit `TOKEN_ERROR` ("I/O read error"), and memory allocation failures emit `TOKEN_ERROR` ("Memory allocation failure") without leaking or emitting corrupted lexemes.

### 3. Spill Buffer Manager (`spill_buffer.h`, `spill_buffer.c`)
- When a token begins in `buffer[0]` and the scan pointer reaches the boundary:
  - If the token has not yet spilled, a spill event begins.
  - The slice from `lexeme_start` to `forward` is saved in the spill buffer.
  - When switching to `buffer[1]`, `lexeme_start` is reset to the beginning of `buffer[1]`.
  - If the token spans across further refills, each intermediate chunk is appended.
  - When the token completes, the tail chunk is appended and the completed string is returned.
- Non-spilled tokens bypass the spill buffer entirely and copy directly from the contiguous buffer slice.

### 4. Baseline vs. PLSB Allocation Policies
- **Conventional Baseline**:
  - Initial spill capacity is fixed to a constant (e.g. 16 or 32 bytes).
  - When the lexeme outgrows capacity, it doubles geometrically: $C_{new} = 2 \cdot C_{current}$.
- **PLSB Prediction Policy**:
  - Dynamically computes initial spill capacity based on the candidate token class, empirical length distribution ($P_{75}$), EWMA length history, and Markov transition context.
  - Clamps initial capacity between a minimum safe floor and a maximum ceiling.
  - Employs the identical safe geometric growth if the token exceeds predicted capacity.
