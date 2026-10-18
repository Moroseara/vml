#!/usr/bin/env python3
"""Mutation check for tests/21: each mutation breaks one std module on purpose; test 21 must fail.
   python3 tools/mutate21.py      (rebuilds a scratch binary per mutation; std/ is restored afterwards)"""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST = "tests/21_std_modules2.vml"

MUTATIONS = [
    ("std/bytes.vml", "d:push(v & 255)", "d:push(v)"),
    ("std/bytes.vml", "if c >= 97 && c <= 102 { return c - 87 }", "if c >= 97 && c <= 102 { return c - 86 }"),
    ("std/bytes.vml", "if n % 2 != 0 {", "if n % 2 == 3 {"),
    ("std/bytes.vml", "if stop > n { stop = n }", ""),
    ("std/bytes.vml", "if a:len() != b:len() { return false }", ""),
    ("std/bytes.vml", "self:push((v >> 8) & 255)", "self:push((v >> 7) & 255)"),
    ("std/bytes.vml", "(b2 << 16)", "(b2 << 15)"),
    ("std/json.vml", "if depth > MAX_DEPTH {", "if depth > 1000 {"),
    ("std/json.vml", "if stop < n && s:byte(stop) == 34 {", "if stop < n && s:byte(stop) == 92 {"),
    ("std/json.vml", "if e == 110 { parts:push(\"\\n\") }", "if e == 110 { parts:push(\"n\") }"),
    ("std/json.vml", "if cp < 128 { return String.from_byte(cp) }", "if cp < 256 { return String.from_byte(cp) }"),
    ("std/json.vml", "        throw f\"control character in string at {i}\"\n", "        i += 1\n"),
    ("std/json.vml", "links[p + COUNT] = links[p + COUNT] + 1", ""),
    ("std/json.vml", "nums[id] = 1.0 as f64", ""),
    ("std/json.vml", "if skip_ws(text, end) != text:len()", "if false"),
    ("std/json.vml", "if i < 0 || i >= self:len(node) { return none }", "if i < 0 { return none }"),
    ("std/json.vml", "else if c == 9 { esc = \"\\\\t\" }", ""),
    ("std/json.vml", "if k == NULL { return \"null\" }", "if k == NULL { return \"nil\" }"),
    ("std/json.vml", "            parts:push(\",\")", ""),
    ("std/json.vml", "if self:kind(node) != STRING { return none }", ""),
    ("std/map.vml", "return self:len() == 0", "return self:len() == 1"),
    ("std/env.vml", "if !has(key) { return none }", ""),
    ("std/os.vml", "/ 1000000000.0", "/ -1000000000.0"),
    ("std/coroutine.vml", "return co.step_id + delta", "return co.step_id + delta + 1"),
    ("std/task.vml", '@[intrinsic("task_defer")]  pub func defer', '@[intrinsic("task_spawn")]  pub func defer'),
    ("std/debug.vml", '@[intrinsic("vm_gc")]            pub func gc()', 'pub func gc() {}'),
]

def build(out):
    subprocess.run(["make", "-s", "build/std_sources.c"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    mk = open(os.path.join(ROOT, "Makefile")).read()
    def srcs(var):
        body = mk.split(var + " =", 1)[1].split("\n\n", 1)[0]
        return body.replace("\\", " ").split()
    cmd = ["clang", "-O1", "-w", "-std=c11"] + srcs("VM_SRC") + srcs("COMPILER_SRC") + srcs("MAIN_SRC") + ["-o", out]
    subprocess.run(cmd, cwd=ROOT, check=True)

def run(binary):
    try:
        p = subprocess.run([binary, TEST], cwd=ROOT, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return False, "timeout"
    ok = p.returncode == 0 and "PASS" in p.stdout
    return ok, (p.stdout + p.stderr).strip().splitlines()[0:1]

def main():
    tmp = tempfile.mkdtemp()
    binary = os.path.join(tmp, "vml_mut")
    build(binary)
    ok, out = run(binary)
    if not ok:
        print("unmutated test 21 fails:", out)
        return 1
    survived = 0
    for i, (path, old, new) in enumerate(MUTATIONS, 1):
        # std/<m>.vml means "the file of module m that contains the pattern" (std/<m>/*.vml)
        mdir = os.path.join(ROOT, path[:-4])
        cands = [os.path.join(mdir, f) for f in sorted(os.listdir(mdir))] if os.path.isdir(mdir) else [os.path.join(ROOT, path)]
        hits = [f for f in cands if old in open(f).read()]
        full = hits[0] if len(hits) == 1 else cands[0]
        orig = open(full).read()
        if orig.count(old) != 1:
            print(f"[{i:2}] SKIP {path}: pattern found {orig.count(old)} times: {old!r}")
            survived += 1
            continue
        try:
            open(full, "w").write(orig.replace(old, new))
            try:
                build(binary)
                ok, out = run(binary)
            except subprocess.CalledProcessError:
                ok, out = False, ["build failed"]
        finally:
            open(full, "w").write(orig)
        status = "SURVIVED" if ok else "killed"
        if ok:
            survived += 1
        print(f"[{i:2}] {status:8} {path}: {old[:60]!r} -> {new[:40]!r}  {out if not ok else ''}")
    subprocess.run(["make", "-s", "build/std_sources.c"], cwd=ROOT, stdout=subprocess.DEVNULL)
    print(f"{len(MUTATIONS) - survived}/{len(MUTATIONS)} mutations killed")
    return 1 if survived else 0

if __name__ == "__main__":
    sys.exit(main())
