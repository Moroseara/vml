# Generates a large source file for measuring compile throughput (lines/sec).
import sys

funcs = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
out = []
for i in range(funcs):
    out.append(f"func f{i}(x: i32, y: i32): i32 {{")
    out.append("    let a: i32 = x * 2 + y")
    out.append("    let b: i32 = a - 3")
    out.append("    if a > b {")
    out.append("        a = a + 1")
    out.append("    } else {")
    out.append("        b = b - 1")
    out.append("    }")
    out.append("    let s: i32 = 0")
    out.append("    let i: i32 = 0")
    out.append("    while i < 10 {")
    out.append("        s = s + i * a")
    out.append("        i = i + 1")
    out.append("    }")
    out.append(f"    return s + b + {i}")
    out.append("}")
    out.append("")
out.append("func main() { print(f0(1, 2)) }")
print("\n".join(out))
