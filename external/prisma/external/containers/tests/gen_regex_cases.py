#!/usr/bin/env python3
"""Runs CPython's `re` over a case table and writes tests/regex_cases.inc.

Every expectation in the .inc file is what Python produced, so test_regex.cpp
checks "same as Python" instead of hand-written guesses. Spans are converted to
UTF-8 byte offsets because ct::Regex works on bytes.

    python3 tests/gen_regex_cases.py
"""
import os
import re
import sys

I, M, S, X = 1, 2, 4, 8


def pyflags(f):
    out = re.ASCII
    if f & I:
        out |= re.IGNORECASE
    if f & M:
        out |= re.MULTILINE
    if f & S:
        out |= re.DOTALL
    if f & X:
        out |= re.VERBOSE
    return out


# (pattern, flags, text, sub_template or None)
CASES = [
    # literals and basics
    ("abc", 0, "xxabcxx", None),
    # empty matches must not split UTF-8 code points
    ("", 0, "é", "-"),
    ("(?:)", 0, "aé", "-"),
    ("[\\x80-\\xbf]", 0, "é", None),
    ("\\B", 0, "", None),
    ("[ab]+|\\B", 0, "", None),
    ("x*", 0, "ééé", "-"),
    # single-step repeats (fast path): greedy/lazy/possessive, classes, dot, fold, multibyte
    ("x*y", 0, "xxxy xy y", None),
    ("x+?y", 0, "xxxy", None),
    ("[^a]*a", 0, "bbbab", None),
    (".*?b", S, "a\nb\nb", None),
    (".*b", 0, "ab\nab", None),
    ("(?i)x*Y+", 0, "xXxyYy", None),
    ("é*a", 0, "éééa", None),
    ("[é-ú]+", 0, "aéíóúb", None),
    ("a*+a", 0, "aaa", None),
    ("a?+a", 0, "a", None),
    ("(a*b)*c", 0, "aabbabc", None),
    ("(?:a*)+b", 0, "aaab", None),
    ("a*a*a*b", 0, "aaaa", None),
    ("x*?", 0, "éxx", "-"),
    ("[a-z]*", 0, "ab12cd", "-"),
    (".+?", 0, "éé", "-"),
    # alternations of single characters
    ("(?:a|b)*c", 0, "abbac xc", None),
    ("(?i:a|B)+x", 0, "aAbBx", None),
    ("(?:[^a]|b)+", 0, "bcdab", None),
    ("(?:\u00e9|a)+", 0, "\u00e9a\u00e9a", None),
    ("(a|b)+", 0, "abba", None),
    ("(?:a|[b-d]|_)+?e", 0, "ab_cde", None),
    ("(?:a|b|c)+", I, "xABcabCx", None),
    ("(?:\\d|\\s)*z", 0, "1 2 3z", None),
    ("a|b|c", 0, "xcbax", "-"),
    # counted repeat whose mandatory iteration matches empty
    ("(|a){1,2}b", 0, "ab", None),
    ("((?<!b)++|(?>a|ab)*+.+?){1,2}(a*)*", 0, "bnn", None),
    ("abc", 0, "xxabxx", None),
    ("", 0, "abc", None),
    ("a", 0, "", None),
    ("a.c", 0, "abc a\nc", None),
    ("a.c", S, "abc a\nc", None),
    ("a|b|c", 0, "xcbax", None),
    ("ab|abc", 0, "abc", None),
    ("abc|ab", 0, "abc", None),
    ("(a|ab)(c|bcd)(d*)", 0, "abcd", None),
    ("hello world", 0, "say hello world!", None),
    ("a\\.b", 0, "a.b axb", None),
    ("\\*\\+\\?", 0, "x*+?y", None),
    ("a\\\\b", 0, "a\\b", None),
    ("\\n\\t", 0, "a\n\tb", None),
    ("\\x41\\x42", 0, "xAB", None),
    ("\\101", 0, "zA", None),
    ("\\07", 0, "a\x07b", None),
    ("\\u00e9", 0, "café", None),
    ("\\U0001F600", 0, "hi \U0001F600!", None),
    ("a{", 0, "a{ a{1", None),
    ("a{}", 0, "a{} a", None),
    ("a{,}", 0, "aaa", None),
    ("a{1,x}", 0, "a{1,x}", None),
    ("}]", 0, "a}]b", None),
    ("]", 0, "a]b", None),
    # quantifiers
    ("a*", 0, "aaab", None),
    ("a+", 0, "baaab", None),
    ("a?", 0, "ab", None),
    ("a?b", 0, "b ab", None),
    ("a{2}", 0, "aaaaa", None),
    ("a{2,}", 0, "a aa aaaa", None),
    ("a{,2}", 0, "aaaa", None),
    ("a{1,3}", 0, "aaaaa", None),
    ("a{0}b", 0, "ab", None),
    ("a*?", 0, "aaa", None),
    ("a+?", 0, "aaa", None),
    ("a??b", 0, "ab", None),
    ("a{2,4}?", 0, "aaaaa", None),
    ("<.*>", 0, "<a><b>", None),
    ("<.*?>", 0, "<a><b>", None),
    ("<.+?>", 0, "<a><b>", None),
    ("a*+a", 0, "aaa", None),
    ("a++b", 0, "aaab", None),
    ("a?+a", 0, "a", None),
    ("a{2,3}+a", 0, "aaaa", None),
    ("(?>a+)b", 0, "aaab", None),
    ("(?>a+)a", 0, "aaa", None),
    ("(?>a|ab)c", 0, "abc", None),
    ("(a*)*b", 0, "aaab", None),
    ("(a*)*b", 0, "aaac", None),
    ("(a*)+b", 0, "b", None),
    ("(a?){3}", 0, "aa", None),
    ("(a?){3}b", 0, "ab", None),
    ("(a{2}){3}", 0, "aaaaaaa", None),
    ("(a|b)*?c", 0, "ababc", None),
    ("(?:ab)*c", 0, "abababc", None),
    ("(?:ab){2,3}", 0, "abababab", None),
    ("(?:a|b)+", 0, "xabbay", None),
    ("x*", 0, "abxd", "-"),
    ("x*", 0, "xxx", "-"),
    ("", 0, "abc", "-"),
    ("a*?", 0, "baaa", "-"),
    ("(a*)*", 0, "aa", None),
    ("(a+|b)*", 0, "ab", None),
    ("(a+|b){0,}", 0, "ab", None),
    ("(a+|b)+", 0, "ab", None),
    ("(a+|b){1,}", 0, "ab", None),
    ("(a+|b)?", 0, "ab", None),
    ("(a+|b){0,1}", 0, "ab", None),
    ("[^ab]*", 0, "cde", None),
    ("a{2,3}", 0, "aaaa", None),
    # groups and captures
    ("(a)(b)(c)", 0, "abc", "\\3\\2\\1"),
    ("(a)|b", 0, "b", None),
    ("(a)|(b)", 0, "b", None),
    ("(?:(a)|b)*", 0, "ab", None),
    ("(?:(a)|b)*", 0, "ba", None),
    ("((a)|(b))*", 0, "ab", None),
    ("(a)*", 0, "aaa", None),
    ("(a*)*", 0, "b", None),
    ("(?P<first>\\w+) (?P<last>\\w+)", 0, "Malcolm Reynolds", "\\g<last> \\g<first>"),
    ("(?P<n>a)(?P=n)", 0, "aa ab", None),
    ("(a)\\1", 0, "aa ab", None),
    ("(a)\\1", I, "aA", None),
    ("(a+)\\1", 0, "aaaa", None),
    ("(a|b)\\1", 0, "ab bb", None),
    ("(?:a)(b)", 0, "ab", None),
    ("()", 0, "x", None),
    ("()*", 0, "x", None),
    ("(a)(b)?", 0, "a", None),
    ("(?:(a)|b)+c", 0, "abc", None),
    ("((((((((((a))))))))))\\10", 0, "aa", None),
    ("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)\\11", 0, "abcdefghijkk", None),
    ("(.)(.)(.)(.)(.)(.)(.)(.)(.)\\9", 0, "abcdefghii", None),
    # classes
    ("[abc]+", 0, "xxcabx", None),
    ("[a-c]+", 0, "xxcabdx", None),
    ("[^abc]+", 0, "abcxyzabc", None),
    ("[a-z0-9_]+", 0, "Hello_w0rld!", None),
    ("[]a]+", 0, "x]a]x", None),
    ("[^]a]+", 0, "]a]xyz]", None),
    ("[a-]+", 0, "x-a-x", None),
    ("[-a]+", 0, "x-a-x", None),
    ("[a\\-z]+", 0, "x-a-z", None),
    ("[\\d]+", 0, "ab123cd", None),
    ("[\\w-]+", 0, "foo-bar baz", None),
    ("[\\s]", 0, "a b", None),
    ("[\\D]+", 0, "12ab34", None),
    ("[\\W]+", 0, "ab, cd", None),
    ("[\\S]+", 0, "  ab  ", None),
    ("[\\n]", 0, "a\nb", None),
    ("[\\x41-\\x43]+", 0, "xABCDx", None),
    ("[\\101-\\103]+", 0, "xABCDx", None),
    ("[\\b]", 0, "a\bb", None),
    ("[\\1\\12\\101]+", 0, "x\x01\nAx", None),
    ("[.]", 0, "a.b", None),
    ("[*+?]", 0, "a+b", None),
    ("[[]", 0, "a[b", None),
    ("[a-z]+", I, "Hello World", None),
    ("[^a-z]+", I, "Hello World", None),
    ("[^A]", I, "a", None),
    ("[^A]", 0, "a", None),
    ("[\\u00e0-\\u00ff]+", 0, "café à la crème", None),
    ("[^\\x00-\\x7f]+", 0, "abcéèxyz中文", None),
    ("[中文]+", 0, "abc中文abc", None),
    ("\\d+", 0, "abc 123 def 4567", None),
    ("\\D+", 0, "123abc456", None),
    ("\\w+", 0, "hello, w0rld_!", None),
    ("\\W+", 0, "hello, w0rld_!", None),
    ("\\s+", 0, "a \t\nb", None),
    ("\\S+", 0, "  ab  cd  ", None),
    ("\\w+", 0, "café olé", None),
    ("\\d", 0, "٣", None),
    # dot and utf-8
    (".", 0, "é", None),
    ("..", 0, "éa", None),
    ("^.$", 0, "中", None),
    ("^.{3}$", 0, "é中\U0001F600", None),
    ("a.b", 0, "aéb", None),
    (".+", 0, "ab\ncd", None),
    (".+", S, "ab\ncd", None),
    ("é+", 0, "caféé", None),
    ("café", I, "CAFé CAFÉ", None),
    # anchors
    ("^abc", 0, "abc abc", None),
    ("^abc", M, "x\nabc\nabc", None),
    ("abc$", 0, "abc abc", None),
    ("abc$", 0, "abc\n", None),
    ("abc$", 0, "abc\n\n", None),
    ("abc$", M, "abc\nabc", None),
    ("^$", 0, "", None),
    ("^$", M, "a\n\nb", None),
    ("^", M, "a\nb\n", "-"),
    ("$", M, "a\nb\n", "-"),
    ("$", 0, "a\n", "-"),
    ("\\Aabc", 0, "abc abc", None),
    ("\\Aabc", M, "x\nabc", None),
    ("abc\\Z", 0, "abc\n", None),
    ("abc\\Z", 0, "abc", None),
    ("\\bfoo\\b", 0, "a foo, foobar foo", None),
    ("\\Bfoo\\B", 0, "a foo, xfooy foo", None),
    ("\\b", 0, "", None),
    ("\\b", 0, "ab cd", "|"),
    ("\\B", 0, "ab cd", "|"),
    ("\\b\\w+\\b", 0, "café été", None),
    ("^a|b$", 0, "b\n", None),
    ("^", 0, "", "-"),
    ("^ab|cd$", M, "xcd\nab", None),
    # lookaround
    ("a(?=b)", 0, "ac ab", None),
    ("a(?!b)", 0, "ab ac", None),
    ("(?<=a)b", 0, "cb ab", None),
    ("(?<!a)b", 0, "ab cb", None),
    ("(?<=ab|cd)x", 0, "abx cdx ex", None),
    ("(?<=\\d{3})x", 0, "12x 123x", None),
    ("(?<=é)x", 0, "ax éx", None),
    ("(?<!é)x", 0, "ax éx", None),
    ("(?=(a+))a*b\\1", 0, "baaabac", None),
    ("(?=(\\w+))\\1:", 0, "foo: bar:", None),
    ("(?!$)", 0, "ab", "-"),
    ("(?=a)", 0, "bab", "-"),
    ("(?<=a)", 0, "bab", "-"),
    ("x(?=(y))", 0, "xy", None),
    ("x(?!(y))", 0, "xz", None),
    ("(?<=^)a", 0, "aa", None),
    ("\\w+(?=,|$)", 0, "ab,cd", None),
    ("(?<=(a))b", 0, "ab", None),
    ("(?<=a)(?<=a)b", 0, "ab", None),
    ("(?!a)(?=b)b", 0, "ab", None),
    # flags
    ("abc", I, "xABCx", None),
    ("ABC", I, "xabcx", None),
    ("a[b-d]e", I, "ACE", None),
    ("(?i)abc", 0, "ABC", None),
    ("(?i:a)b", 0, "Ab AB", None),
    ("(?i)(?-i:a)b", 0, "aB AB", None),
    ("(?s).", 0, "\n", None),
    ("(?m)^b", 0, "a\nb", None),
    ("(?im)^b", 0, "a\nB", None),
    ("(?x) a b  c # comment", 0, "abc", None),
    ("a b", X, "ab a b", None),
    ("a\\ b", X, "ab a b", None),
    ("a[ ]b", X, "ab a b", None),
    ("a #c\nb", X, "ab", None),
    ("(?x: a b )c", 0, "abc", None),
    ("a  +", X, "aaa", None),
    ("a  {2}", X, "aaa", None),
    ("(?#comment)ab", 0, "ab", None),
    ("a(?#comment)b", 0, "ab", None),
    ("(?a)\\w+", 0, "abé", None),
    ("\\u00e9", I, "É", None),
    ("[\\u00e9]", I, "É", None),
    ("\\w", I, "é", None),
    # empty matches and iteration
    ("a*", 0, "baaab", None),
    ("", 0, "", None),
    ("$", 0, "", "x"),
    ("^|a", 0, "aa", "-"),
    ("a|", 0, "ba", "-"),
    ("|a", 0, "ba", "-"),
    ("\\b|x", 0, "ax bx", "-"),
    ("(?:)", 0, "ab", "-"),
    ("a??", 0, "aa", "-"),
    ("(a|)b", 0, "ab b", None),
    # sub templates
    ("(\\w+) (\\w+)", 0, "hello world foo bar", "\\2 \\1"),
    ("(?P<a>x)", 0, "xyx", "\\g<a>\\g<a>"),
    ("(x)", 0, "axa", "\\g<1>-\\g<0>"),
    ("x", 0, "axa", "\\n\\t\\\\"),
    ("x", 0, "axa", "\\."),
    ("(x)(y)?", 0, "x", "[\\2]"),
    ("a", 0, "aaa", "b"),
    ("a", 0, "aaa", ""),
    ("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)", 0, "abcdefghij", "\\10\\1"),
    ("(a)", 0, "aa", "\\1\\g<1>\\g<01>"),
    ("x", 0, "x", "\\07\\012"),
    # split
    ("[,;]", 0, "a,b;c", None),
    ("([,;])", 0, "a,b;c", None),
    ("x*", 0, "axbc", None),
    ("\\s*", 0, "a b", None),
    ("(,)|(;)", 0, "a,b;c", None),
    (",", 0, ",a,,b,", None),
    (",", 0, "", None),
    ("\\b", 0, "a b", None),
    # misc python doc examples
    ("(?P<word>\\b\\w+\\b)", 0, "Lots of punctuation", None),
    ("([a-z]+)@([a-z]+)\\.(com|org)", 0, "mail bob@site.org now", None),
    ("(\\d+)\\.(\\d+)", 0, "pi is 3.14159 and e is 2.71828", "\\2.\\1"),
    ("(?:\\d{1,3}\\.){3}\\d{1,3}", 0, "ip 192.168.1.1 here", None),
    ("^(?:(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)$", 0, "192.168.1.255", None),
    ("^(?:(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)$", 0, "192.168.1.256", None),
    ("<([a-z]+)>.*?</\\1>", 0, "<b>bold</b> <i>it</i>", None),
    ("(\\w)(\\w)\\2\\1", 0, "xabbay", None),
    ("\\b(\\w+)\\s+\\1\\b", I, "the The cat", None),
    ("(a)(?:(b)|(c))(d)", 0, "acd", None),
    ("^(a+)+$", 0, "aaaaaaaaaaaab", None),
    ("^(\\w+)\\s*=\\s*(.*)$", M, "key = value\nother=thing", None),
    ("\\$\\{(\\w+)\\}", 0, "a ${x} b ${yy}", "<\\1>"),
    ("[A-Z][a-z]+", 0, "Hello World From Ct", None),
    ("\\d{4}-\\d{2}-\\d{2}", 0, "on 2026-09-11 and 2027-01-02", None),
    ("(?i)^(?:true|false)$", 0, "TRUE", None),
    ("^\\s*$", 0, "   ", None),
    ("^\\s*$", M, "a\n   \nb", None),
    ("a{3,5}?b", 0, "aaaaab", None),
    ("(a|b)*c\\1", 0, "abcb", None),
    ("(?:a{2})*", 0, "aaaaa", None),
    ("(a{2})*", 0, "aaaaa", None),
    ("([ab]*)+b", 0, "ab", None),
    ("(a|ab)*c", 0, "ababc", None),
    ("(a|ab)*?c", 0, "ababc", None),
]

ERRORS = [
    "(",
    ")",
    "a)",
    "(a",
    "[a",
    "[",
    "[]",
    "*a",
    "a**",
    "a{2}{3}",
    "a{3,2}",
    "+",
    "?",
    "^*",
    "$*",
    "\\b*",
    "\\A+",
    "\\",
    "a\\",
    "\\q",
    "\\p",
    "[\\q]",
    "\\x4",
    "\\xZZ",
    "\\u12",
    "\\U110000",
    "\\1",
    "(a)\\2",
    "(a\\1)",
    "[z-a]",
    "[a-\\d]",
    "[\\d-z]",
    "(?P<n>a)(?P<n>b)",
    "(?P=x)",
    "(?P<>a)",
    "(?P<1a>a)",
    "(?P<a-b>a)",
    "(?<=a+)b",
    "(?<=a|bc)x",
    "(?<=^|,)\\w+",
    "(?<=(a)\\1)x",
    "(?z)",
    "(?",
    "(?P",
    "(?Px",
    "(?<x",
    "(?#abc",
    "(?i",
    "a(?i)b",
    "(?-)",
    "(?i-)",
    "(?-a:x)",
    "(?ii-i:x)",
    "(?L)",
    "(?:a",
    "(?=a",
    "a|*",
    "(*)",
    "(?#x)*",
    "[\\8]",
    "(?<name>a)",
]

SPECIAL_CHARS = {
    '"': '\\"', '\\': '\\\\', '?': '\\077',
}


def cstr(s):
    if s is None:
        return "nullptr"
    if isinstance(s, str):
        s = s.encode("utf-8")
    out = ['"']
    for b in s:
        ch = chr(b)
        if ch in SPECIAL_CHARS:
            out.append(SPECIAL_CHARS[ch])
        elif 32 <= b < 127:
            out.append(ch)
        else:
            out.append("\\%03o" % b)
    out.append('"')
    return "".join(out)


def byte_off(text, i):
    return len(text[:i].encode("utf-8"))


def spans(m, text):
    parts = []
    for g in range(m.re.groups + 1):
        s, e = m.span(g)
        if s < 0:
            parts.append("-")
        else:
            parts.append("%d,%d" % (byte_off(text, s), byte_off(text, e)))
    return ";".join(parts)


def emit_list(items, limit=24):
    items = list(items)
    if len(items) > limit - 1:
        raise SystemExit("too many list items for case: %r" % (items,))
    return "{" + ", ".join(cstr(x) for x in items) + ("," if items else "") + " nullptr}"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(here, "regex_cases.inc")
    lines = []
    lines.append("// Generated by tests/gen_regex_cases.py with Python %d.%d.%d. Do not edit." % sys.version_info[:3])
    lines.append("// {kind, pattern, flags, text, text_len, pos, template, ok, spans, list}")
    n = 0
    for pattern, flags, text, template in CASES:
        rx = re.compile(pattern, pyflags(flags))
        p, t = cstr(pattern), "%s, %d, %d" % (cstr(text), len(text.encode("utf-8")), byte_off(text, 1) if len(text) > 1 else 0)

        m = rx.search(text)
        lines.append("{\"search\", %s, %d, %s, nullptr, %d, %s, {nullptr}}," % (p, flags, t, 1 if m else 0, cstr(spans(m, text) if m else "")))
        n += 1
        m = rx.match(text)
        lines.append("{\"match\", %s, %d, %s, nullptr, %d, %s, {nullptr}}," % (p, flags, t, 1 if m else 0, cstr(spans(m, text) if m else "")))
        n += 1
        m = rx.fullmatch(text)
        lines.append("{\"fullmatch\", %s, %d, %s, nullptr, %d, %s, {nullptr}}," % (p, flags, t, 1 if m else 0, cstr(spans(m, text) if m else "")))
        n += 1
        matches = list(rx.finditer(text))
        lines.append("{\"findall\", %s, %d, %s, nullptr, %d, nullptr, %s}," % (p, flags, t, len(matches), emit_list(m.group(0) for m in matches)))
        n += 1
        all_spans = ";".join(spans(m, text) for m in matches)
        lines.append("{\"finditer\", %s, %d, %s, nullptr, %d, %s, {nullptr}}," % (p, flags, t, len(matches), cstr(all_spans)))
        n += 1
        parts = ["" if x is None else x for x in rx.split(text)]
        lines.append("{\"split\", %s, %d, %s, nullptr, %d, nullptr, %s}," % (p, flags, t, len(parts), emit_list(parts)))
        n += 1
        parts = ["" if x is None else x for x in rx.split(text, 1)]
        lines.append("{\"split1\", %s, %d, %s, nullptr, %d, nullptr, %s}," % (p, flags, t, len(parts), emit_list(parts)))
        n += 1
        for tpl in (["<\\g<0>>"] + ([template] if template is not None else [])):
            r = rx.sub(tpl, text)
            lines.append("{\"sub\", %s, %d, %s, %s, 0, nullptr, %s}," % (p, flags, t, cstr(tpl), emit_list([r])))
            n += 1
            r = rx.sub(tpl, text, 1)
            lines.append("{\"sub1\", %s, %d, %s, %s, 0, nullptr, %s}," % (p, flags, t, cstr(tpl), emit_list([r])))
            n += 1
        if len(text) > 1:
            m = rx.search(text, 1)
            lines.append("{\"search1\", %s, %d, %s, nullptr, %d, %s, {nullptr}}," % (p, flags, t, 1 if m else 0, cstr(spans(m, text) if m else "")))
            n += 1
            m = rx.match(text, 1)
            lines.append("{\"match1\", %s, %d, %s, nullptr, %d, %s, {nullptr}}," % (p, flags, t, 1 if m else 0, cstr(spans(m, text) if m else "")))
            n += 1
    for pattern in ERRORS:
        try:
            re.compile(pattern, re.ASCII)
        except re.error as e:
            lines.append("{\"error\", %s, 0, nullptr, 0, 0, nullptr, 0, %s, {nullptr}}," % (cstr(pattern), cstr(str(e))))
            n += 1
        else:
            raise SystemExit("pattern %r compiles in Python, remove it from ERRORS" % pattern)
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s: %d checks" % (out_path, n))


if __name__ == "__main__":
    main()
