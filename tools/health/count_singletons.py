#!/usr/bin/env python3
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

PAT = re.compile(r'\bgetInstance\s*\(')

def iter_files():
    for dirpath, _, filenames in os.walk(ROOT):
        # Skip common build dirs
        if any(seg in dirpath for seg in (os.sep + "build", os.sep + ".git", os.sep + ".vcpkg")):
            continue
        for fn in filenames:
            if fn.endswith((".h", ".hpp", ".cpp", ".cxx", ".inl")):
                yield os.path.join(dirpath, fn)

def main():
    total = 0
    hits = []
    for path in iter_files():
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                text = f.read()
        except OSError:
            continue
        n = len(PAT.findall(text))
        if n:
            total += n
            hits.append((n, os.path.relpath(path, ROOT)))

    hits.sort(reverse=True)
    print(f"getInstance() call sites: {total}")
    for n, p in hits[:25]:
        print(f"  {n:3d}  {p}")
    if total == 0:
        return 0
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
