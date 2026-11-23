#!/usr/bin/env python3
"""Host C tests for pure logic inside drivers/s23gpu (no WDK, no hardware).

The production sources are NOT modified or copied by hand: the code under test is extracted from
drivers/s23gpu at build time (between fixed markers) into a temporary directory and compiled together
with small user-mode shims. If a marker disappears, the extraction fails loudly.

  python tests/c/run_c_tests.py [--sanitize] [--cc COMPILER]

--sanitize adds -fsanitize=address,undefined (gcc/clang on Linux/macOS).
Exit code 0 = all tests passed, 1 = a build or test failed, 77 = no C compiler found.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
GPU = os.path.join(ROOT, "drivers", "s23gpu")

TESTS = ["test_va_alloc.c", "test_msm_validate.c", "test_alloc_range.c"]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read().replace("\r\n", "\n")


def between(text, start, end, what):
    a = text.find(start)
    b = text.find(end, a + len(start)) if a >= 0 else -1
    if a < 0 or b < 0:
        raise SystemExit(f"extract: markers for {what} not found ({start!r} .. {end!r})")
    return text[a:b]


def extract(outdir):
    hw = read(os.path.join(GPU, "hw.c"))
    hdr = read(os.path.join(GPU, "s23gpu.h"))
    ddi = read(os.path.join(GPU, "ddi.c"))

    # VA allocator: VA_NODE + list head, and the five *Locked functions
    node = between(hw, "typedef struct _VA_NODE {", "static VA_NODE *g_VaFreeHead = NULL;", "VA_NODE")
    funcs = between(hw, "/* Reusable GPU VA interval allocator (guarded by g_HwLock) */",
                    "/* Dynamic 3-level", "VA allocator functions")
    defs = [m.group(0) for m in re.finditer(r"^#define S23GPU_VA_USER_(START|END)\s+\S+", hdr, re.M)]
    if len(defs) != 2:
        raise SystemExit("extract: S23GPU_VA_USER_START/END not found in s23gpu.h")
    with open(os.path.join(outdir, "va_alloc_extracted.inc"), "w", encoding="utf-8") as f:
        f.write("/* generated from drivers/s23gpu/s23gpu.h + hw.c by tests/c/run_c_tests.py */\n")
        f.write("\n".join(defs) + "\n")
        f.write(node + "static VA_NODE *g_VaFreeHead = NULL;\n\n" + funcs)

    # TgCreateAllocation BO range check (regression guard for the 64-bit bo_offset + size fix)
    m = re.search(r"if \((al->Bo == NULL \|\|\s*[^{]*?)\)\s*\{\s*\n\s*if \(al->Bo != NULL\)", ddi)
    if not m:
        raise SystemExit("extract: BO range check in TgCreateAllocation (ddi.c) not found")
    cond = " ".join(m.group(1).split())
    with open(os.path.join(outdir, "alloc_range_check.inc"), "w", encoding="utf-8") as f:
        f.write("/* generated from drivers/s23gpu/ddi.c (TgCreateAllocation) by tests/c/run_c_tests.py */\n")
        f.write(f"#define S23_ALLOC_RANGE_REJECT(al) ({cond})\n")


def find_cc(explicit):
    for cand in ([explicit] if explicit else []) + [os.environ.get("CC"), "cc", "gcc", "clang"]:
        if cand and shutil.which(cand):
            return cand
    return None


def main(argv):
    sanitize = "--sanitize" in argv
    cc = None
    if "--cc" in argv:
        i = argv.index("--cc")
        cc = argv[i + 1] if i + 1 < len(argv) else None
    cc = find_cc(cc)
    if not cc:
        print("run_c_tests: no C compiler found (set CC)")
        return 77
    failed = 0
    with tempfile.TemporaryDirectory(prefix="s23ctest-") as out:
        extract(out)
        flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                 f"-I{out}", f"-I{GPU}", f"-I{HERE}"]
        if sanitize:
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
        for t in TESTS:
            exe = os.path.join(out, os.path.splitext(t)[0] + (".exe" if os.name == "nt" else ""))
            cmd = [cc] + flags + [os.path.join(HERE, t), "-o", exe]
            print("CC:", " ".join(cmd), flush=True)
            if subprocess.call(cmd) != 0:
                print(f"BUILD FAILED: {t}")
                failed += 1
                continue
            rc = subprocess.call([exe])
            print(f"{t}: {'PASS' if rc == 0 else f'FAIL (rc={rc})'}", flush=True)
            failed += rc != 0
    print(f"C host tests: {len(TESTS) - failed}/{len(TESTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
