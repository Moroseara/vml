// Playground examples. Each one is run by tools/check_web_examples.js against vml.wasm,
// so the site never shows code that does not compile.
(function (root) {
  "use strict";
  const EXAMPLES = [
    {
      id: "hello",
      title: "Hello",
      code: `## Strings, loops and f-strings
func main() {
    let langs = ["vml", "C", "Rust"]
    for i, name in langs {
        print(f"{i + 1}. hello from {name}")
    }
    let total = 0
    for n in 1..101 { total += n }
    print(f"1 + 2 + ... + 100 = {total}")
}
`,
    },
    {
      id: "errors",
      title: "Errors as values",
      code: `## A function that can fail says so in its type: int ! ParseError
enum ParseError { Empty, BadChar(at: int) }

func parse_digit(s: string): int ! ParseError {
    if #s == 0 { throw ParseError.Empty }
    let d = "0123456789":find(s)
    if #s != 1 || d < 0 { throw ParseError.BadChar(0) }
    return d
}

## ? passes the error to the caller
func add(a: string, b: string): int ! ParseError {
    return parse_digit(a)? + parse_digit(b)?
}

func main() {
    print(add("4", "5") catch -1)          ## 9
    print(add("4", "x") catch -1)          ## -1
    let why = add("", "1") catch e {
        print(match e {
            Empty => "empty input",
            BadChar(at) => f"bad character at {at}"
        })
        0
    }
    let z = 0
    let safe = 10 / z catch e { print("caught: " + e); 0 }
}
`,
    },
    {
      id: "enums",
      title: "Enums and match",
      code: `enum Shape {
    Circle(r: float),
    Rect(w: float, h: float),
    Empty
}

func area(s: Shape): float {
    return match s {
        Circle(r) => 3.14159 * r * r,
        Rect(w, h) => w * h,
        Empty => 0.0
    }
}

## T? is a value that may be missing
func find(xs: string[], want: string): int? {
    for i, x in xs {
        if x == want { return i }
    }
    return none
}

func main() {
    print(area(Shape.Circle(2.0)))
    print(area(Shape.Rect(3.0, 4.5)))
    let names = ["ann", "bob"]
    print(find(names, "bob") ?? -1)
    print(find(names, "zed") ?? -1)
}
`,
    },
    {
      id: "closures",
      title: "Closures",
      code: `func make_counter(): (): i32 {
    let count: i32 = 0
    return func(): i32 {
        count += 1
        return count
    }
}

func apply_twice(f: (x: i32): i32, v: i32): i32 {
    return f(f(v))
}

func main() {
    let next = make_counter()
    next()
    next()
    print(f"counter: {next()}")

    let step: i32 = 5
    print(apply_twice(func(x: i32): i32 { return x + step }, 1))
}
`,
    },
    {
      id: "json",
      title: "JSON",
      code: `import std.json

func empty_doc(): JsonDoc {
    let d: JsonDoc
    return d
}

func main() {
    ## ""[ ... ] is a raw string: no escapes needed, may span lines
    let text = ""[
{"name": "vml", "ports": [80, 443], "debug": false}
]
    let doc = json.parse(text) catch e { print("bad JSON: " + e); empty_doc() }
    print(doc:get_str(doc:root(), "name") ?? "?")
    print(doc:int(doc:find("ports.1") ?? 0) ?? 0)

    let w = JsonWriter.new()
    w:begin_object()
    w:key("ok") w:bool(true)
    w:key("pi") w:num(3.14159 as f64)
    w:end_object()
    print(w:to_string())
}
`,
    },
    {
      id: "modules",
      title: "Standard library",
      code: `import std.math
import std.fmt
import std.bytes

func main() {
    print(math.gcd(84, 36))
    print(math.pow(2, 20))
    print(fmt.thousands(1234567))
    print(fmt.hex(48879))

    let b = Bytes.from_string("vml")
    b:push_u16(513)
    print(b:to_hex())

    let words = "the quick brown fox":split(" ")
    print(words:len())
    print("vml":upper() + " " + "  trim me  ":trim())
}
`,
    },
    {
      id: "sandbox",
      title: "Sandbox",
      code: `## The host decides which modules a script may use.
## Turn modules off in "Sandbox" below and run this again.
import std.random
import std.time
import std.os

func main() {
    let seed = time.unix()
    print(f"dice: {random.range(seed, 1, 7)}")
    print(f"running on {os.platform()}")

    ## Fuel stops runaway scripts: lower it below and watch this loop stop
    let n = 0
    for i in 2000000 { n += i % 3 }
    print(f"counted {n}")
}
`,
    },
  ];
  if (typeof module !== "undefined" && module.exports) module.exports = EXAMPLES;
  else root.VML_EXAMPLES = EXAMPLES;
})(typeof self !== "undefined" ? self : this);
