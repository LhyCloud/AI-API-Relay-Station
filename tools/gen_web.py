# -*- coding: utf-8 -*-
"""Generate src/web.cpp from the panel assets (embedded as raw string literals)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "src"))
OUT = os.path.join(SRC, "web.cpp")

ASSETS = [
    ("loginHtml", "login.html"),
    ("loginJs", "login.js"),
    ("hashJs", "hash.js"),
    ("adminHtml", "admin.html"),
    ("adminJs", "admin.js"),
]

LIMIT = 11000


def split_chunks(text, limit):
    chunks = []
    cur = []
    size = 0
    for line in text.splitlines(True):
        if size and size + len(line) > limit:
            chunks.append("".join(cur))
            cur = []
            size = 0
        cur.append(line)
        size += len(line)
    if cur:
        chunks.append("".join(cur))
    return chunks


def main():
    parts = ['#include "relay.h"\n\n']
    funcs = []
    for name, filename in ASSETS:
        path = os.path.join(SRC, filename)
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        if ")HTML\"" in text:
            sys.exit(filename + " contains the raw-string terminator")
        chunks = split_chunks(text, LIMIT)
        ids = []
        for i, chunk in enumerate(chunks, 1):
            ident = "k%s%d" % (name[0].upper() + name[1:], i)
            parts.append('static const char* %s = R"HTML(%s)HTML";\n' % (ident, chunk))
            ids.append(ident)
        funcs.append((name, ids))
        print("%s: %d chunks, %d chars" % (filename, len(chunks), len(text)))

    body = []
    for name, ids in funcs:
        expr = " + ".join(("std::string(%s)" % ids[0]) if i == 0 else ids[i] for i in range(len(ids)))
        body.append("const std::string& %s() {\n    static const std::string s = %s;\n    return s;\n}\n"
                    % (name, expr))
    parts.append("\n" + "\n".join(body))
    with open(OUT, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write("".join(parts))


if __name__ == "__main__":
    main()
