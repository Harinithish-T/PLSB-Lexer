"""Display-only token grouping for the dashboard.

The native C lexer emits preprocessing punctuation as OP_HASH plus the
following tokens on that line. Native equivalence checks keep that stream.
The dashboard groups a line-leading '#' directive into one PREPROCESSOR row.
"""


def group_preprocessor_display_tokens(tokens, source_text=None):
    """Combine a start-of-line '#' directive into one PREPROCESSOR token.

    A hash is treated as a directive only when it is the first native token
    on its source line. Strings and comments never emit OP_HASH, so a '#'
    inside those constructs is left alone.
    """
    if not tokens:
        return []

    lines = None
    if source_text is not None:
        lines = source_text.splitlines()

    grouped = []
    i = 0
    n = len(tokens)
    while i < n:
        tok = tokens[i]
        type_name = str(tok.get("type", "")).upper()
        is_hash = type_name in ("OP_HASH", "TOKEN_OP_HASH")
        prev_line = tokens[i - 1].get("line") if i > 0 else None
        first_on_line = prev_line != tok.get("line")
        if is_hash and first_on_line:
            j = i + 1
            while j < n and tokens[j].get("line") == tok.get("line"):
                j += 1
            group = tokens[i:j]
            line_no = tok.get("line", 1)
            col = tok.get("col", 1)
            if lines is not None and 1 <= int(line_no) <= len(lines):
                line = lines[int(line_no) - 1]
                start = max(0, int(col) - 1)
                lexeme = line[start:].rstrip("\r")
            else:
                lexeme = "".join(str(t.get("lexeme", "")) for t in group)
            grouped.append({
                "line": line_no,
                "col": col,
                "type": "PREPROCESSOR",
                "lexeme": lexeme,
                "spilled": any(bool(t.get("spilled")) for t in group),
                "length": len(lexeme),
            })
            i = j
        else:
            grouped.append(dict(tok))
            i += 1
    return grouped


def token_streams_equivalent(a, b):
    """Token-level equivalence: type, lexeme, line, and column must match."""
    if a is None or b is None:
        return False, 0.0
    if len(a) != len(b):
        return False, 0.0
    if not a:
        return True, 100.0
    matches = 0
    for left, right in zip(a, b):
        if (
            left.get("type") == right.get("type")
            and left.get("lexeme") == right.get("lexeme")
            and left.get("line") == right.get("line")
            and left.get("col") == right.get("col")
        ):
            matches += 1
    pct = 100.0 * matches / len(a)
    return matches == len(a), pct
