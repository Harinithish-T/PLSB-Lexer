#!/usr/bin/env python3
"""Regression tests for dashboard preprocessor grouping (display layer only)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "dashboard"))
from token_display import group_preprocessor_display_tokens, token_streams_equivalent


def tok(line, col, type_name, lexeme, spilled=False):
    return {
        "line": line,
        "col": col,
        "type": type_name,
        "lexeme": lexeme,
        "spilled": spilled,
        "length": len(lexeme),
    }


def assert_eq(cond, msg):
    if not cond:
        raise AssertionError(msg)


def test_include_stdio():
    source = "#include <stdio.h>\nint x;\n"
    native = [
        tok(1, 1, "OP_HASH", "#"),
        tok(1, 2, "IDENTIFIER", "include"),
        tok(1, 10, "OP_LT", "<"),
        tok(1, 11, "IDENTIFIER", "stdio"),
        tok(1, 16, "OP_DOT", "."),
        tok(1, 17, "IDENTIFIER", "h"),
        tok(1, 18, "OP_GT", ">"),
        tok(2, 1, "KW_INT", "int"),
        tok(2, 5, "IDENTIFIER", "x"),
        tok(2, 6, "OP_SEMI", ";"),
    ]
    grouped = group_preprocessor_display_tokens(native, source)
    assert_eq(grouped[0]["type"] == "PREPROCESSOR", "include is PREPROCESSOR")
    assert_eq(grouped[0]["lexeme"] == "#include <stdio.h>", grouped[0]["lexeme"])
    assert_eq(grouped[0]["line"] == 1 and grouped[0]["col"] == 1, "line/col preserved")
    assert_eq(len([t for t in grouped if t["type"] == "PREPROCESSOR"]) == 1, "one directive")
    ok, _ = token_streams_equivalent(native, native)
    assert_eq(ok, "native stream still equivalent to itself")


def test_define_and_leading_space():
    source = "#define SIZE 100\n  #include <x.h>\n"
    native = [
        tok(1, 1, "OP_HASH", "#"),
        tok(1, 2, "IDENTIFIER", "define"),
        tok(1, 9, "IDENTIFIER", "SIZE"),
        tok(1, 14, "INT_DEC", "100"),
        tok(2, 3, "OP_HASH", "#"),
        tok(2, 4, "IDENTIFIER", "include"),
        tok(2, 12, "OP_LT", "<"),
        tok(2, 13, "IDENTIFIER", "x"),
        tok(2, 14, "OP_DOT", "."),
        tok(2, 15, "IDENTIFIER", "h"),
        tok(2, 16, "OP_GT", ">"),
    ]
    grouped = group_preprocessor_display_tokens(native, source)
    assert_eq(grouped[0]["lexeme"] == "#define SIZE 100", grouped[0]["lexeme"])
    assert_eq(grouped[1]["type"] == "PREPROCESSOR", "indented include grouped")
    assert_eq(grouped[1]["col"] == 3, "starting column of '#'")
    assert_eq(grouped[1]["lexeme"].startswith("#include"), grouped[1]["lexeme"])


def test_hash_in_comment_and_string():
    source = '/* # not a directive */\nchar *s = "#include";\n'
    native = [
        tok(1, 1, "COMMENT", "/* # not a directive */"),
        tok(2, 1, "KW_CHAR", "char"),
        tok(2, 6, "OP_STAR", "*"),
        tok(2, 7, "IDENTIFIER", "s"),
        tok(2, 9, "OP_ASSIGN", "="),
        tok(2, 11, "STRING", '"#include"'),
        tok(2, 21, "OP_SEMI", ";"),
    ]
    grouped = group_preprocessor_display_tokens(native, source)
    types = [t["type"] for t in grouped]
    assert_eq("PREPROCESSOR" not in types, "hash in comment/string is not a directive")
    assert_eq(len(grouped) == len(native), "no grouping applied")


def test_multiple_and_eof():
    source = "#include <a.h>\n#define Z 1"
    native = [
        tok(1, 1, "OP_HASH", "#"),
        tok(1, 2, "IDENTIFIER", "include"),
        tok(1, 10, "OP_LT", "<"),
        tok(1, 11, "IDENTIFIER", "a"),
        tok(1, 12, "OP_DOT", "."),
        tok(1, 13, "IDENTIFIER", "h"),
        tok(1, 14, "OP_GT", ">"),
        tok(2, 1, "OP_HASH", "#"),
        tok(2, 2, "IDENTIFIER", "define"),
        tok(2, 9, "IDENTIFIER", "Z"),
        tok(2, 11, "INT_DEC", "1"),
    ]
    grouped = group_preprocessor_display_tokens(native, source)
    pre = [t for t in grouped if t["type"] == "PREPROCESSOR"]
    assert_eq(len(pre) == 2, "two directives")
    assert_eq(pre[1]["lexeme"] == "#define Z 1", pre[1]["lexeme"])


def main():
    tests = [
        test_include_stdio,
        test_define_and_leading_space,
        test_hash_in_comment_and_string,
        test_multiple_and_eof,
    ]
    failed = 0
    for fn in tests:
        try:
            fn()
            print(f"  -> PASS: {fn.__name__}")
        except Exception as ex:
            failed += 1
            print(f"  -> FAIL: {fn.__name__} ({ex})")
    print(f"Results: {len(tests) - failed} passed, {failed} failed")
    sys.exit(failed)


if __name__ == "__main__":
    main()
