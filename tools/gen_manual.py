#!/usr/bin/env python3
"""docs/manual.txt -> assets/manual.h  (pages start with '== TITLE'; <=14 lines of <=50 cols)"""
import sys
src, out = sys.argv[1], sys.argv[2]
pages = []
for line in open(src, encoding="utf-8"):
    line = line.rstrip("\n")
    if line.startswith("== "): pages.append([line[3:]])
    else: pages[-1].append(line)
for i, p in enumerate(pages):
    assert len(p) - 1 <= 14, "page %d has %d lines" % (i + 1, len(p) - 1)
    for l in p[1:]: assert len(l) <= 50, "page %d line too long: %r" % (i + 1, l)
# chapter starts: first page whose title begins with a new chapter number
starts, seen = [], set()
for i, p in enumerate(pages):
    ch = p[0].split(".")[0] if p[0][0].isdigit() else "0"
    if ch not in seen: seen.add(ch); starts.append(i)
def c(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
with open(out, "w", encoding="utf-8") as f:
    f.write("// Generated from docs/manual.txt by tools/gen_manual.py -- edit the .txt\n#pragma once\n")
    f.write("#define MANUAL_PAGES %d\n#define MANUAL_LINES 15\n" % len(pages))
    f.write("static const char *kManual[MANUAL_PAGES][MANUAL_LINES] = {\n")
    for p in pages:
        f.write("  { " + ", ".join(c(l) for l in p) + (", 0" * (15 - len(p))) + " },\n")
    f.write("};\n#define MANUAL_CHAPTERS %d\nstatic const int kManualChapterStart[MANUAL_CHAPTERS] = { %s };\n"
            % (len(starts), ", ".join(map(str, starts))))
print("wrote", out, len(pages), "pages,", len(starts), "chapters")
