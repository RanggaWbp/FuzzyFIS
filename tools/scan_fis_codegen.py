#!/usr/bin/env python3
"""Auto-discover every tracked *.fis, regenerate its generated header, and
optionally compile all generated headers. Used by CI so that newly added
fixtures are exercised without editing .github/workflows/ci.yml.

Modes:
  (default) regenerate headers in place (committed sibling *_fis.h) or under
            /tmp otherwise; print the manifest.
  --compile  re-generate and compile every generated header as C99 and C++11.

Fixtures whose FuzzyFIS cannot be emitted (e.g. the non-MATLAB
FZ_DEFUZZ_CENTROID_INTEGRAL extension) are reported and skipped, not failed.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIS2H = os.path.join(ROOT, "tools", "fis2h.py")
UNSUPPORTED_MARK = "centroid_integral"


def sh(args, cwd=ROOT, **kw):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True, **kw)


def tracked_fis():
    r = sh(["git", "ls-files", "*.fis"])
    if r.returncode != 0:
        print(r.stderr, file=sys.stderr)
        sys.exit(1)
    return [l for l in r.stdout.split() if l]


def is_tracked(path):
    return sh(["git", "ls-files", "--error-unmatch", path]).returncode == 0


def generate(fis):
    """Emit the header for one .fis. Returns (header_path, skipped_flag)."""
    base = fis[: -len(".fis")]
    out = os.path.join(ROOT, base + "_fis.h") if is_tracked(base + "_fis.h") \
        else "/tmp/" + os.path.basename(base) + "_fis.h"
    r = sh(["python3", FIS2H, fis, "--source", fis, "-o", out])
    if r.returncode != 0:
        if UNSUPPORTED_MARK in (r.stderr + r.stdout):
            print(f"skip (unsupported defuzz) {fis}", file=sys.stderr)
            return None, True
        print(f"FAIL {fis}: {r.stderr.strip()}", file=sys.stderr)
        sys.exit(1)
    return out, False


def compile_headers(headers):
    for h in headers:
        for cc, std, lang in (("gcc", "c99", "c"), ("g++", "c++11", "c++")):
            r = sh([cc, f"-std={std}", "-Wall", "-Wextra", "-Werror",
                    "-pedantic", "-Isrc", "-Iexamples/generated_static",
                    "-x", lang, "-c", h, "-o", "/dev/null"])
            if r.returncode != 0:
                print(f"COMPILE FAIL {h} [{cc} -std={std}]",
                      r.stderr.strip(), file=sys.stderr)
                sys.exit(1)


def main():
    compile_only = "--compile" in sys.argv
    headers, skipped = [], 0
    for fis in tracked_fis():
        h, was_skipped = generate(fis)
        if was_skipped:
            skipped += 1
            continue
        headers.append(h)
        if not compile_only:
            print("gen", fis, "->", os.path.relpath(h, ROOT))
    if compile_only:
        compile_headers(headers)
        print(f"compiled {len(headers)} generated headers clean (C99 + C++11)")
        if skipped:
            print(f"skipped {skipped} unsupported fixture(s)", file=sys.stderr)
    else:
        print(f"regenerated {len(headers)} headers, skipped {skipped}")


if __name__ == "__main__":
    main()
