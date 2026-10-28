/* =========================================================================================
 * Benchmark Fixture: long_comments.c
 * Characteristics: Extended header documentation, licensing terms, architectural rationale,
 * and multi-paragraph commentary designed to cross input buffer boundaries repeatedly.
 * =========================================================================================
 *
 * MIT License / Academic Research License Notice:
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * DETAILED ARCHITECTURAL NOTES ON STREAMING LEXICAL INPUT BUFFERING:
 * In traditional compilers dating back to the Dragon Book (Aho, Sethi, Ullman, 1986),
 * the standard input buffering scheme employs two contiguous buffers of equal size N,
 * where N typically matches a disk block or filesystem page (4096 bytes).
 * When scanning large lexical items—such as this extensive comment block—the lexeme
 * can span beyond the current buffer into the alternate buffer, necessitating careful
 * spill-buffer accumulation or boundary-crossing pointers.
 *
 * If the input buffer size is configured to a smaller value (such as 16, 32, or 64 bytes),
 * this very paragraph will span dozens of consecutive buffer refills.
 * Under conventional baseline doubling policies, multiple reallocations occur, each
 * copying all previously accumulated bytes to newly allocated heap blocks.
 * Under Predictive Lexeme Spill Buffering (PLSB), empirical token-class statistics
 * predict an initial capacity matching the 75th percentile (P75) of previous comment lengths,
 * dramatically curtailing geometric doubling cascades.
 * =========================================================================================
 */

#include <stdio.h>

/* Small utility function following the extensive block comment */
int compute_hash(const char *str) {
    /*
     * Secondary block comment documenting Dan Bernstein's djb2 hash algorithm.
     * The multiplier 33 has proven effective across varied ASCII distribution spaces.
     * Note that this comment is deliberately sized to test boundary transitions.
     */
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return (int)hash;
}
