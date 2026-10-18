# Language Foundation — Draft v0.1

> สถานะ: ร่างไว้ให้ตัดสินใจ  ลบหรือขีดฆ่าข้อที่ไม่เอาได้เลย
> ✅ = ตกลงกันแล้ว  ⬜ = เสนอ รอยืนยัน

---

## 0. หลักการ

- ✅ **VM ล้วน ระดับเดียว** — bytecode interpreter ตัวเดียว ไม่มี AOT / JIT
- ✅ **Standalone + Embed** — โปรแกรมเดียวกันรันได้ทั้งแบบ CLI และฝังใน host
- ✅ **Cross-device** — แกน VM เป็น C99 ไม่พึ่ง libc, bytecode format เดียวทุกเครื่อง
- ✅ **keyword น้อยที่สุด** — ความสามารถอยู่ใน module ไม่ใช่ใน syntax
- ⬜ **Static type** — compiler รู้ type ทุกค่า → typed opcode เร็ว, error ตั้งแต่ compile
- ⬜ **ไม่มี global ที่เปลี่ยนได้** — ไม่มี `getenv`/`setenv` แบบ Roblox

---

## 1. Syntax พื้นฐาน

```
let x = 10                 // ตัวแปร (type อนุมานเอง)
let y: float = 1.5         // ระบุ type
const MAX = 100            // ค่าคงที่
x = x + 1

func add(a: int, b: int): int {
    return a + b
}

if x > 5 { ... } else { ... }
while x < 10 { ... }
for i in 10 { ... }        // 0..9
for item in list { ... }
for k, v in map { ... }
```

- ⬜ `int` = i64 เป็นค่าเริ่มต้น (กันเลขล้นเงียบๆ แบบที่เจอตอนนี้), มี `i32`/`f32` ให้เลือกเมื่อต้องการ
- ⬜ ไม่มี `;` (ใส่ได้แต่ไม่บังคับ)

## 2. การเรียกฟังก์ชัน

```
test("a", 2)               // ปกติ
test "a", 2                // command call (ต้นบรรทัดเท่านั้น)
test { a = "a", b = 2 }    // named args
test("a", 2) { ... }       // trailing block = closure เป็น arg สุดท้าย
test "a", 2 { ... }
```

กฎ (✅ ทำแล้ว — `tests/16_call_syntax.vml`):
- ✅ command call `f a, b` ได้เฉพาะต้น statement และ argument ต้องอยู่บรรทัดเดียวกัน
- ✅ ใน command call: `b()` ติดกัน = เรียก `b` / `b ()` เว้นวรรค = เรียกผลลัพธ์ต่อ (`test "p", 2 ()`)
- ✅ `{ name = ... }` = named args (สลับลำดับได้, ที่ไม่ใส่ใช้ค่า default)
- ✅ `{ ... }` / `{ a, b -> ... }` = trailing block → closure เป็น arg สุดท้าย, type ของ `a, b` มาจาก callback parameter, expression สุดท้ายคือค่าที่คืน
- ✅ ห้าม trailing block ในเงื่อนไข `if`/`while`/`for`/`match` (`{` ตรงนั้นคือ body เสมอ)
- ⬜ `{ a, b }` แบบ positional (ยังไม่รองรับ — กำกวมกับ block)

## 3. Type และข้อมูล

```
struct Player {
    pub name: string
    hp: int = 100
}

enum Shape {
    Circle(r: float)
    Rect(w: float, h: float)
}

let area = match s {
    Circle(r)  => 3.14 * r * r
    Rect(w, h) => w * h
}

let name: string? = find(id)       // optional
let n = name?.len ?? 0
```

- ✅ **ไม่มี nil** — ใช้ `T?` เท่านั้น
- ✅ `struct`, `enum` (sum type), `match` แบบครบทุกกรณี (exhaustive)
- ✅ Generic: `func first<T>(xs: T[]): T?`
  - เดา `T` จาก argument ได้ (`first(names)`) หรือระบุเอง (`first<string>(names)`)
  - เรียกได้โดยไม่ส่งค่า: `zero<Point>()`, `parse<int>(s)` — ใน body ใช้ `T is int` ได้ และ branch ที่ไม่ตรงถูกตัดทิ้งตอน compile
  - `T` เป็น struct, `T?`, `T ! E` ได้; แต่ละชุด type compile ครั้งเดียวแล้วใช้ซ้ำ (`larger<i64>`)
- ⬜ Interface: `type Drawable { draw: (self): void }` (มีอยู่แล้วในชื่อ `type` contract)
- ⬜ เก็บ metamethod เดิม (`operator +`, `[]`, `#`, `iter` ...) ไว้

## 4. Function และ Closure

```
let add = func(a: int, b: int): int { return a + b }
let inc = { x: int -> x + 1 }               // lambda สั้น

func make_counter(): (): int {
    let n = 0
    return { n = n + 1; n }                  // closure ที่ส่งออกไปได้
}
```

- ⬜ Closure แบบเต็ม: จับตัวแปรแยกต่อ closure, ส่งออกนอกฟังก์ชัน, เก็บใน array/map ได้
- ⬜ type ฟังก์ชันเขียนได้ทุกที่: `let f: (int): int = ...`

## 5. Error

```
func parse_int(s: string): int ! ParseError {
    if #s == 0 { throw ParseError.Empty }   // คืน error
    ...
    return n                                // คืนค่าปกติ (ห่อเป็น ok ให้อัตโนมัติ)
}

let n = parse_int(s)?              // ส่ง error ต่อ
let n = parse_int(s) catch 0       // ค่า default
let n = parse_int(s) catch e {     // จัดการเอง
    log.warn(e.message)
    0
}
panic("bug")                       // bug จริง: หยุดพร้อม stack trace
```

- ✅ `T ! E` เป็นค่า (struct แบน {ok, value, error}) — ไม่มี cost ตอนไม่ error, 1M call ≈ 0.02s
- ✅ E เป็นอะไรก็ได้: string, int, enum (มี payload), struct (`e.code`, `e.message`)
- ✅ `throw e`, `f()?` (error type ต้องตรงกับของฟังก์ชัน), `catch v`, `catch e { ...; ค่า }`, `void ! E`
- ✅ แทน `pcall` / `xpcall` ทั้งหมด — `f() catch e { }` กับค่าที่ไม่ใช่ result = ดัก runtime error / `panic` (e = ข้อความ)
- ✅ ลืมจัดการ error ที่ type บอกไว้ → compile ไม่ผ่าน (ทิ้งผลลัพธ์, ใช้เป็นค่าปกติ, print, คำนวณ)
- ✅ หารด้วย 0 (int) → runtime error ที่ catch ได้ ไม่ทำ VM ล่ม

## 6. Module

```
## std/json/mod.vml
module std.json   ## namespace ของไฟล์ (ไม่มีบรรทัดนี้ = ใช้ชื่อจาก path)
import "./parse"   ## ไฟล์ภายใน: ส่วนหนึ่งของ std.json, เห็น private
import "./helpers" {slug} as t   ## module อื่นแบบ relative: เห็นเฉพาะ pub
import std.math {sin as s, PI} as m

const MAX_DEPTH: i32 = 128   ## private
pub struct JsonDoc { ... }   ## json.JsonDoc
pub func parse(text: string): JsonDoc ! string { ... }   ## json.parse
```
```
import std.json
let doc = json.parse(text)?
import game   ## module ที่ host register ให้ — หน้าตาเหมือนกันทุกตัว
```

### ชื่อและการมองเห็น
- ✅ `import` แบบเดียว ไม่มี `extern`, host register module พร้อม type signature (ข้อ 10)
- ✅ ชื่อ module ซ้อนได้ไม่จำกัดชั้น: `import game.ui.widgets.button` → `<root ของ game>/ui/widgets/button.vml` (หรือ `.../button/mod.vml`); handle = ท่อนสุดท้าย
  - ชื่อเต็ม ≤ 127 ตัว (⬜ ไม่จำกัด)
- ✅ `module x.y` = namespace ของไฟล์ — ไม่มี = ชื่อจาก path; `module` เป็น keyword เฉพาะหัวไฟล์ (ใช้เป็นชื่อตัวแปรได้)
  - ชื่อที่ใช้ในโค้ด (handle) = ท่อนสุดท้ายของ import หรือชื่อหลัง `as`; module ที่ท่อนสุดท้ายชนกันต้องใช้ `as` (ไม่งั้น error)
- ✅ `pub` เป็นทางเดียวที่จะแชร์ — ไม่มี `pub` = private ของ module (เรียก `util.helper()` จากข้างนอก = error); `pub func` ระดับบนเรียกเป็น `ชื่อmodule.func`
  - generic ที่ instantiate จากข้างนอกยังเป็นของ module ที่ประกาศ (ใช้ private helper ได้)
- ✅ **ตัด keyword `export` ออกจากภาษา** — namespace มาจาก `module` เท่านั้น, การแชร์มาจาก `pub` เท่านั้น
  - ย้าย std ครบแล้ว: `Math.sqrt` → `math.sqrt`, `Json.parse` → `json.parse`, ... (type จริงอย่าง `Bytes`, `JsonDoc`, `Coroutine` ยังเรียก static method ได้: `Bytes.new()`)
  - เจอ `export` = compile error พร้อมบอกให้ใช้ `module x.y` + `pub`
  - (bug เดิมที่หายไปพร้อมกัน: `export X as a.b` เก็บแค่ท่อนสุดท้าย, `export Evil as math` ทับ std ได้)
- 🟡 ใครโหลด module คนนั้นตั้งชื่อ — กฎเดียวทุกที่มา: host/C/Builder (`vml_module_new(L, "game")`), `.vmb`, `.vml` (`module x.y`)

### import ไฟล์ (relative path)
- ✅ `import "./x"` — ต้องขึ้นต้นด้วย `./` หรือ `../` (นับจากไฟล์ที่ import), ห้ามออกนอก folder ของ package (`../` ที่หลุด root = error)
  - root = folder ของโปรแกรมหลัก หรือ folder ของ package ที่โหลดผ่าน `--lib-path` / `--pkg`
- ✅ ไฟล์ปลายทางไม่มี `module` (หรือชื่อตรงกับเรา) = **ไฟล์ภายใน**: เห็น private, compile รวมเข้า `.vmb` ของเจ้าของ, ใช้ `{...}` / `as` ไม่ได้ (error)
- ✅ ไฟล์ปลายทางมี `module` ชื่ออื่น = **module อื่น**: เห็นเฉพาะ `pub`, ใช้ `{a as b}` / `as t` ได้
- ✅ แต่ละไฟล์ถูกรวมครั้งเดียว — ⬜ import วน = error ที่บอกเส้นทาง
- ⬜ `vml interface` แสดงว่าไฟล์ไหนเป็นส่วนของ module ไหน

### นามสกุลและไฟล์คู่ (ใช้กับ module อื่นเท่านั้น — ไฟล์ภายในหาแค่ `.vml`)
- 🟡 ไม่ใส่นามสกุล → ตอนนี้ใช้ `.vml`; ⬜ เลือกเอง `.vmb` → `.vmi` → `.vml` (มาพร้อม build cache)
- ✅ ใส่ `.vmb` / `.vmi` ตอนนี้ = error ชัดเจนว่ายังไม่รองรับ
- ⬜ ใส่นามสกุล → ไฟล์นั้นเป็นตัวหลัก, ไฟล์คู่ (ชื่อเดียวกัน folder เดียวกันเท่านั้น) เป็นตัวช่วยเมื่อ hash ตรง:

| import | ตัวหลัก | ไฟล์คู่ |
|---|---|---|
| `"./x.vml"` | source | `x.vmb` ที่ source hash ตรง → ใช้แทน ไม่ compile ซ้ำ; ไม่ตรง → compile ใหม่ |
| `"./x.vmb"` | bytecode | `x.vml` ใช้แค่เลขบรรทัดใน error/traceback (ไม่ compile); hash ไม่ตรง → แสดงแค่ชื่อฟังก์ชัน |
| `"./x.vmi"` | type อย่างเดียว | ตอนรันหา `x.vmb` → `x.vml`; interface ต้องตรงกับ `.vmi` ไม่งั้น error ชัดเจน |

- ⬜ `.vmb` เก็บ hash ของ source + hash ของ interface ตัวเอง + hash ของ interface ทุก module ที่ import; `.vmi` เก็บ hash ของ interface

### compile เฉพาะส่วนที่เปลี่ยน (หน่วย = module)
- ⬜ 1 module = 1 `.vmb` (ไฟล์ภายในรวมอยู่ในนั้น)
- ⬜ แก้แค่ข้างใน (interface hash เดิม) → compile ใหม่เฉพาะ module นั้น; module ที่ import มันไม่ต้อง compile
- ⬜ แก้ signature ของ `pub` → compile ใหม่เฉพาะ module ที่ import มันตรงๆ
- ⬜ std ที่ฝังใน binary: ฝังเป็น bytecode แทน source (ข้อ 9)
- ไม่ทำรายฟังก์ชัน: compile ~10 ms / 1500 ฟังก์ชัน → หน่วย module พอ; ค่อยพิจารณาถ้ามี module ใหญ่มาก

### กันปลอมเป็น module อื่น
หลักการ: สิทธิ์ขยายชื่อมาจากเจ้าของ, สิทธิ์แทนที่มาจาก host เท่านั้น — package ที่ import มาไม่มีทั้งสองอย่าง
- ✅ override overload ที่มีอยู่ (signature เดียวกัน) ไม่ได้ — compile error
- ข้างล่างนี้ปิดแล้ว (เดิมทำได้): package เพิ่ม overload `String.upper(self, n)`, `export Evil as math` ทับ std, เรียก private ของ module อื่น, `import my.util` + `import other.util` ชนกันเงียบๆ
- ✅ ทุกฟังก์ชันเก็บ module ต้นทาง (`func_module` ใน compiler) — alias ยัง ⬜
- ✅ เพิ่ม overload ให้ชื่อของ module อื่นไม่ได้ (รวม `String.*` ของ std) — error บอกเจ้าของ
- ⬜ `@[open]`: เจ้าของเปิดให้เพิ่ม overload ได้ และเห็นเฉพาะไฟล์ที่ import ตรง
- ⬜ method ชื่อใหม่บน type คนอื่น (`String.shout`) ได้ — เห็นเฉพาะไฟล์ที่ import, ห้ามชนชื่อเดิม
- ⬜ เจอตัวที่ตรงจาก 2 แหล่ง = error "ambiguous" พร้อมบอกที่มา ห้ามเลือกเงียบๆ
- ✅ namespace เป็นของ module ที่ลงทะเบียนก่อน — จองซ้ำ = error
- ✅ `std.*` สงวนให้ไฟล์ที่ฝังใน binary; `module` ที่ประกาศต้องอยู่ใต้ root ของ path ที่ใช้โหลด (`evil/x.vml` ประกาศ `my.*` = error)
- ⬜ แทนที่ module ทำได้ทางเดียว: host ลงทะเบียนแทนก่อนโหลด (ข้อ 10)
- ✅ test: `tests/fail/*.vml` (ทับ overload, เพิ่ม overload, อ้างเป็น std, อ้าง root อื่น, `export`, เรียก private, ชื่อ module ชน) ต้อง compile ไม่ผ่านพร้อมข้อความที่ถูก — `tools/run_fail_tests.sh` อยู่ใน `make test`; `tests/22_modules.vml` (package ใน `tests/pkgs`)

### อื่นๆ
- ⬜ สร้างไฟล์ interface `.vmi` อัตโนมัติ สำหรับ editor / LSP
- ⬜ bug: `build_mod_path` ตัด `--lib-path` ที่ยาวเกิน 80 ตัวอักษรเงียบๆ → หา module ไม่เจอ
- ✅ อ่านไฟล์ module ได้ไม่จำกัดขนาด/จำนวน (เดิม 16 ไฟล์ × 32KB)
- ⬜ `vml build` ยังไม่รับ `--lib-path`

### ประกาศแยกจากตัวฟังก์ชัน: `type func` (แบบ header ของ C)
```
## mod.vml — หน้า API ของ module
module fxmod
import "./mod_part"
type func hidden(): int            ## ประกาศ signature (ไม่มี body)

## mod_part.vml — ไฟล์ภายใน
pub func hidden(): int { return 1 }   ## ตัวจริง
```
- ✅ ประกาศด้วย `type func name(...): T` เท่านั้น — `func f()` ที่ลืม `{` ยังเป็น syntax error (กันพิมพ์ผิดกลายเป็นประกาศ)
- ✅ ตัวจริงอยู่ไฟล์ไหนก็ได้ของ module และ signature ต้องตรงเป๊ะ — ไม่มีตัวจริง / signature ต่าง = compile error ชี้ไปที่ประกาศ (เดิมพังตอนรัน)
  - ⚠️ ใช้ `type func` เป็น callback type ล้วนๆ โดยไม่มีตัวจริงไม่ได้แล้ว → ใช้ `type cb = (x: int): int`
- 🟡 `pub` ที่ตัวจริงใช้ได้; ⬜ `pub type func` ที่ประกาศ
- `type hidden = (): int` คือ type ของฟังก์ชัน (ตัวแปร / callback) ไม่ใช่ประกาศ — แยกกันชัด
- ⬜ syntax เดียวกับ `.vmi` (ไฟล์ interface = รายการ `type func` / `type struct` ล้วน)

### `pub import` (ส่งต่อ module ให้คนที่ใช้เรา)
```
module game
pub import "./ui"          ## ข้างนอกเห็น game.ui ด้วย
pub import mathx.ops       ## ส่งต่อ dep
```
- ✅ `pub import`: namespace และชื่อยังเป็นของเจ้าของ → ไม่เปิดช่องปลอมตัวแบบ `export X as y`
- ✅ ปิดรู import รั่ว: module ที่ไม่ได้ import ตรง (หรือได้ผ่าน `pub import`) เรียกด้วยชื่อไม่ได้ (เดิม `import game` แล้วเรียก `ui.title()` ของ module ที่ game ใช้ได้) — method ของค่า (`doc:get()`, `Bytes.new()`) ไม่ถูกจำกัด, prelude เห็นทุกที่
  - ⬜ ใช้กับ const ของ module ด้วย (ตอนนี้เฉพาะฟังก์ชัน)
- ✅ `pub import` ไฟล์ภายใน = error (ไฟล์ภายในใช้ `pub` ของตัวเองอยู่แล้ว)

## 6b. Project, Dependency และ Build cache

```toml
# vml.toml — ที่ root ของ project (ข้อมูลล้วน ไม่ใช่โค้ดที่รันได้)
[project]
name    = "game"          # root namespace: module game, game.ui, ...
version = "0.3.1"
entry   = "src/main.vml"

[deps]
vecmath = { path = "../vecmath" }
json5   = { git = "https://github.com/x/json5", version = "1.2" }
mylog   = { git = "...", version = "0.4", as = "log" }   # เปลี่ยนชื่อ root ฝั่งคนใช้
```
```
game/
  vml.toml   vml.lock         # lock: version + hash ของทุก dep (commit)
  src/main.vml                # module game
  src/ui/button.vml           # module game.ui
  tests/
  .vml-cache/                 # build cache (ไม่ commit)
```

### ที่อยู่ของ source (ไม่ต้องตั้งค่าก็ได้ / กำหนดเองได้ทุกอย่าง)
| ระดับ | เขียน | ได้ |
|---|---|---|
| ไม่มี `vml.toml` | — | `vml app.vml` + `import "./x"` (แบบ C) |
| แค่ชื่อ | `name = "game"` | มี `src/` → ใช้ `src/`; ไม่มี → folder ของ project |
| กำหนด root | `source = "code"` / `"."` | module เริ่มจาก folder นั้น |
| ทีละ module | `[modules]` `"game.ui" = "client/ui/mod.vml"` | ไฟล์อยู่ไหนก็ได้ (ห้ามออกนอก project) |

- ✅ module แบบ folder: `import game.ui` → `ui.vml` ถ้าไม่มี → `ui/mod.vml` (std, `--pkg`, `--lib-path`, project)
- ✅ ไฟล์ไหนนับเป็น source (ข้อบนชนะ): `include` / `exclude` ใน `vml.toml` → `.vmlignore` → `.gitignore` (`respect_gitignore = true` default) → default ในตัว (`.vml-cache/`, `dist/`, `.git/`, folder ที่ขึ้นต้นด้วย `.`, และ `tests/` เมื่อ `source = "."`)
  - ใช้กับ cache key แล้ว; ⬜ ใช้กับการหา module ด้วย (ตอนนี้ import ไฟล์ที่ถูก ignore ยังได้) + error บอกว่าถูก ignore โดยไฟล์ไหน
  - syntax: `name`, `dir/`, `/anchored`, `*`, `?`, `**`, `!` — อ่านเฉพาะ `.gitignore` / `.vmlignore` ที่ root ของ source (⬜ ไฟล์ซ้อนใน folder ย่อย)
  - ไฟล์ที่มีอยู่แต่ถูก ignore → error บอกว่าถูก ignore โดยไฟล์ไหน
  - ignore ไม่ใช่ความปลอดภัย — ตัวกันจริงคือ "ห้ามออกนอก folder ของ package"

- ✅ `source = "..."` (ห้ามออกนอก project), ไม่ระบุ = `src/` ถ้ามี ไม่งั้น folder ของ project; ⬜ `[modules]` ชี้ทีละ module

### Workspace (หลาย package ใน repo เดียว)
```toml
[workspace]
members = ["std", "examples/demo", "tools/*"]

[workspace.deps]
mathx = { path = "libs/mathx" }
```
- ✅ member แต่ละตัวมี `vml.toml` + ชื่อ root ของตัวเอง; อ้างกันด้วย `x = { workspace = true }` (path จาก `[workspace.deps]`), members รองรับ `"tools/*"`
- ✅ `vml test` ที่ root ทดสอบทุก member, `-p <member>` เฉพาะตัว, ในโฟลเดอร์ member = ทำกับ member นั้น
- ⬜ `.vml-cache/` + `vml.lock` ร่วมกันที่ root
- ✅ std คือ member แรก: `std/vml.toml` (`name = "std"`, `builtin = true` — ใช้ชื่อ `std` ได้เฉพาะแบบนี้ และไม่ถูก mount เพราะ binary ให้ std เอง)

### คำสั่ง
- ✅ `vml new <name>` (โครง + ตัวอย่าง module + test + `.gitignore`) / `vml run [-v]` / `vml build [-o file]` (→ โปรแกรม `dist/<name>`) / `vml test` (ทุกไฟล์ใน `tests/`) / `vml clean`
- ⬜ `vml add <dep>`, bundle
- ✅ `vml build x.vml -o x.vmb` แบบเดิมยังใช้ได้ และรับ `--lib-path` / `--pkg` แล้ว
- ✅ `vml file.vml` แบบไม่มี project ยังใช้ได้ (สคริปต์ไฟล์เดียว)
- ✅ หา `vml.toml` จาก folder ปัจจุบันขึ้นไปด้านบน; มี project แล้วไม่ต้องใช้ `--pkg` / `--lib-path`
- ✅ module ของ project อยู่ใน `src/`: `import game.ui` → `src/ui.vml` (ไฟล์ต้องประกาศ `module game.ui` หรือไม่ประกาศ)
  - ⬜ ชื่อ 3 ชั้นขึ้นไป (`game.ui.button` → `src/ui/button.vml`) — ตอนนี้ใช้แค่ชั้นแรกกับชั้นสุดท้าย
- ✅ embed API: `vml_mount_package(L, name, dir)`, `vml_set_lib_path(L, dir)`

### Build cache
- ✅ ขั้นแรก: `.vml-cache/main.vmb` ทั้งโปรแกรม + `main.key` — key = hash ของตัว `vml` เอง + ทุก `vml.toml` + ทุก `.vml` ใน `src/` ของ project และทุก dep; ไม่มีอะไรเปลี่ยน = ไม่ compile เลย
  - build พัง → ลบ key ก่อน compile จึงไม่มีทางรัน image เก่า
- ⬜ `.vmb` ต่อ module (compile เฉพาะ module ที่เปลี่ยน) — key = hash ของ source + hash ของ interface ที่ import + version ของ compiler (ต้องมี `.vmb` ต่อ module ข้อ 10 ก่อน)
- ✅ compiler เปลี่ยน (binary ต่าง) → cache ใช้ไม่ได้อัตโนมัติ
- ⬜ dep ที่ดึงมาแล้วอยู่ใน cache กลาง `~/.vml/pkg/<name>@<version>` ใช้ร่วมกันทุก project

### Dependency และความปลอดภัย
- ✅ ชื่อ root ของ package มาจาก `name` ใน `vml.toml` — package ประกาศ module ได้เฉพาะ `<name>` / `<name>.*`; `std` ใช้ไม่ได้
- ✅ dep แบบ `path` (ซ้อนต่อกันได้, ชื่อ key ต้องตรงกับ `name` ของ dep, สองแพ็กเกจชื่อซ้ำ = error)
- ⬜ ชื่อชนกัน → ฝั่งคนใช้แก้ด้วย `as = "..."` ไม่ต้องแก้ package (ตอนนี้ = error ชัดเจนว่ายังไม่รองรับ)
- ⬜ `vml.lock` เก็บ hash เนื้อหาของทุก dep — เนื้อหาเปลี่ยนจากที่ล็อก (เช่น tag ถูกย้าย) = build ไม่ผ่าน
- ⬜ std จัดเป็น package เหมือนกัน (`std/<module>/...`) แต่ฝังใน binary และไม่ต้องประกาศใน `[deps]`

### Build pipeline (แบบ CMakeLists แต่เป็นข้อมูล)
```toml
[build]
targets = ["app", "tools"]

[target.app]
kind    = "program"            # program | library | bundle
entry   = "src/main.vml"
output  = "dist/app.vmb"
features.disable = ["fs", "env"]    # module ที่ host ไม่ให้ใช้ (ข้อ 8)

[target.tools]
kind    = "library"
modules = ["game.ui", "game.net"]

[[step]]                        # ขั้นก่อน/หลัง build (สร้างโค้ด, copy asset)
before = "app"
run    = "tools/gen_bindings.vml"   # รันด้วย vml เอง ไม่ใช่ shell → ข้ามเครื่องได้, sandbox ได้
```
- ⬜ อยู่ใน `vml.toml` ไฟล์เดียวกับ project — เป็นข้อมูล ไม่ใช่ภาษา script แบบ CMake (อ่านง่าย tool อื่นอ่านได้ ไม่มีโค้ดแปลกๆ รันตอน build)
- ⬜ ขั้นที่ต้องคำนวณจริง (สร้างโค้ด, แปลง asset) = สคริปต์ `.vml` ที่ build รันให้ ได้แค่สิทธิ์ที่ประกาศ (เช่น `fs` เฉพาะ folder project)
- ⬜ target หลายแบบ: program (`.vmb`), library (`.vmb` + `.vmi`), bundle (ไฟล์เดียวรวม dep)
- ⬜ profile: `vml build --release` (ตัด debug info, เปิด optimize) / `--dev`
- ⬜ host C ที่ฝัง vml: `vml build` ออก `.vmb` + header ให้ CMake/Make ของ host ดึงไปใช้ (ไม่แทนที่ build ของ C)

- ✅ test: `tools/test_project.sh` (อยู่ใน `make test`) — new/run/build/test/clean, cache hit/miss, แก้ไฟล์ = rebuild, build พังไม่ใช้ image เก่า, dep แบบ path, dep ปลอมชื่อ, error ของ manifest

### ลำดับ
1. ✅ ระบบ module (ข้อ 6) — project/cache สร้างบนนี้
2. ✅ project + dep แบบ `path` + `.vml-cache/` (cache ทั้งโปรแกรม)
3. build pipeline: targets, profiles, ขั้น `.vml` ก่อน/หลัง build; ย้าย std เป็น `std/<module>/` หลายไฟล์
4. dep แบบ `git` + `vml.lock` + cache กลาง (ต้องดาวน์โหลด แยกไว้หลังสุด)

## 6c. CLI และเครื่องมือ — ประตูเดียว แยกชั้นข้างใน

```
libvml          VM + compiler + loader (vm/ + compiler/) — ทุกเครื่องมือ link ตัวนี้
  ├─ vml        ประตูหลัก: run / build / test / clean / new
  │             vml pkg add|remove|update   vml fmt / check / doc / interface
  │             vml <x> → หา vml-<x> ใน PATH แล้วรัน (แบบ git / cargo)
  ├─ vml-analyzer   LSP (binary แยก, ใช้ compiler ตัวเดียวกัน → error ตรงกับ build)
  └─ host ภายนอก   ฝัง libvml ผ่าน vml.h
```
- ✅ `src/cli/`: `main.c` (ตาราง dispatch + `vml help [cmd]`), `script.c`, `image.c`, `project.c`
- ✅ Makefile: `build/libvml.a` (object ต่อไฟล์, `-j` ได้, dependency จาก header) แล้ว `vml` / `embed_host` link ตัวนั้น
- ✅ `vml <x>` → `vml-<x>` ใน PATH (ไม่เจอ = exit 2)
- ⬜ flag เดียวกันทุกคำสั่ง (`-v`, `-o`, `--release`, `-p`), `--json` ให้เครื่องอ่าน
- ⬜ exit code: 0 ผ่าน, 1 error ของโค้ด, 2 ใช้คำสั่งผิด, 3 error ของ manifest
- ⬜ `~/.vml/`: cache dep, เครื่องมือที่ติดตั้ง, config เครื่อง
- ⬜ API ภายในสำหรับเครื่องมือ (parse, symbol, type ที่ตำแหน่ง) อยู่ใน libvml ไม่ใช่ `vml.h` (ไม่ผูก ABI)

## 7. Coroutine และ Task

สองชั้น ใช้ fiber ตัวเดียวกันใน VM (ไม่ใช่ OS thread):

| | `coroutine` | `task` |
|---|---|---|
| ใครเป็นคนสั่งรัน | ผู้เขียนเอง (`resume` ด้วยมือ) | scheduler ของ VM |
| ใช้ทำอะไร | generator, state machine, AI ในเกม, cutscene | I/O, งานพร้อมกัน, timer |
| keyword | ไม่มี — เป็น module | ไม่มี — เป็น module |

### coroutine — ควบคุมเอง
```
import coroutine

let gen = coroutine.new { n: int ->
    for i in n { coroutine.yield(i) }
}

while let v = gen.resume(3) {        // resume คืน T? — จบแล้วได้ none
    print(v)                         // 0, 1, 2
}

gen.status                           // .suspended / .running / .done / .failed
```
- ⬜ type ชัด: `Coroutine<In, Out>` — ค่าที่ส่งเข้า `resume` และค่าที่ `yield` ออก มี type
- ⬜ `for v in gen { }` ใช้กับ coroutine ได้ตรงๆ (เป็น iterator)
- ⬜ error ใน coroutine → `resume` คืน error ตามระบบข้อ 5 ไม่ทำให้ VM ล่ม

### task — ให้ scheduler จัดการ (สร้างบน coroutine)
```
import task

let a = task.spawn { fetch(url1) }
let b = task.spawn { fetch(url2) }
let (x, y) = task.wait(a, b)

task.sleep(0.5)
task.scope {                         // ออกจาก scope = รอ/ยกเลิกงานลูกทั้งหมด
    task.spawn { worker(1) }
}
```
- ✅ `task.spawn` เป็น library ไม่ใช่ keyword
- ⬜ `task.scope` แบบ structured, `task.cancel(t)`, `task.timeout(sec) { }`
- ⬜ host ขับ scheduler ได้: `vml_tick(L, dt)` ทุกเฟรม (เกม) หรือ `vml_run_until_idle(L)` (CLI/server)
- ⬜ ฟังก์ชัน C ที่ register (ทาง B) suspend task ได้ — เช่น host I/O แบบ async คืนค่ากลับมาทีหลัง

## 8. Environment และสิทธิ์

```
import env
import fs

let home = env.get("HOME")         // ค่า environment ของ OS (อ่านอย่างเดียว)
let text = fs.read("a.txt")?
```

- ⬜ ตัด `getenv` / `setenv` แบบ Roblox ออก
- ⬜ sandbox ทำที่ฝั่ง host: host เลือกว่าจะ register module ไหนให้สคริปต์ (`fs`, `net`, `env` ...) — ไม่ register = สคริปต์ import ไม่ได้
- ⬜ (ทางเลือก) capability แบบ `func main(sys: System)` — **ผมแนะนำให้ตัด** เพราะข้อบนให้ผลเดียวกันแต่ง่ายกว่า

## 9. Standard Library

- ✅ เขียนด้วย `.vml` เป็นหลัก บน **intrinsic ชุดเล็ก** (88 ตัวใน `vm/stdlib/vm_intrinsics.c`: string/bytes, array, map, math, clock, fs, env/os, io, task, debug, hash)
  - ประกาศใน stdlib: `@[intrinsic("str_upper")] pub func String.upper(self): string` (ใช้ได้เฉพาะไฟล์ใน `std/`)
  - intrinsic แบบ opcode ถูก **inline ตรงจุดเรียก** (ไม่มี CALL) — compiler ไม่มีโค้ดพิเศษสำหรับ `Math.*` แล้ว
  - intrinsic แบบ native อยู่ในฟังก์ชัน `$ชื่อ` → `.vmb` bind กลับด้วยชื่อได้
  - intrinsic สำหรับสแกน string (`str_skip_ws`, `str_scan_plain`, `str_span_num`): loop ต่อ byte อยู่ใน C, logic อยู่ใน `.vml` → `json.parse` เร็วขึ้น ~3× (bench `json_parse`: 0.112s → 0.035s)
- 🟡 ฝังใน binary: ไฟล์ `std/*.vml` ถูกฝังเป็น source ตอน build (`tools/embed_std.c`) แล้ว compile ตอน import (~0.3 ms สำหรับ prelude) — ฝังเป็น bytecode รอ `.vmb` ต่อ module (ข้อ 10)
- ✅ prelude (โหลดอัตโนมัติ): `string` (split, lines, count, pad, to_int → `int?`, …), `array` (index_of, contains, sum, reverse, join, …), `map` (has, get, set, remove, keys, clear, is_empty)
- ✅ `import`: `math` (int/i32/f32/f64 overload, pow, gcd, lerp), `random`, `bit`, `fmt` (fixed, hex, thousands, pad), `time`, `fs` (ทุกฟังก์ชันคืน `T ! string`; host ปิดได้ด้วย `VM_FEAT_STD_FS`)
- ✅ ย้ายเป็น `.vml` แล้ว: `task`, `env` (อ่านอย่างเดียว, `get` → `string?`), `os`, `debug`, `io` (`read_line` → `string?`), `coroutine` (ยังเป็นตัวนับขั้น รอ fiber ข้อ 7)
- ✅ ใหม่: `bytes` (`Bytes`: from_string/from_hex/to_hex, slice, append, equals, u16/u32 little-endian) และ `json` (`Json.parse` → `JsonDoc ! string`, `get_*`/`find("a.b.0")`/`to_string`, `JsonWriter`, `Json.quote`)
  - `JsonDoc` เป็นตารางแบน (ไม่สร้าง object ต่อค่า), ซ้อนลึกได้ 128 ชั้น (เกินแล้วเป็น error ที่ catch ได้ ไม่ใช่ stack overflow)
- ✅ ทดสอบ: `tests/21_std_modules2.vml` ครอบทุก module ข้างบน + `tools/mutate21.py` (26 mutation ใน std ต้องทำให้ test 21 fail — ตอนนี้ 26/26) ผ่านทั้งปกติ, `--gc-stress` และรันจาก `.vmb`
- ✅ std เป็น package จริง: `std/<module>/mod.vml` (+ ไฟล์ภายใน เช่น `json/parse.vml`), `std/vml.toml`, `std/tests/`; ฝังทั้ง tree ลง binary (key = path) และ `import "./x"` ใน std อ่านจากไฟล์ที่ฝัง (โปรแกรมผู้ใช้เข้าถึงด้วย path ไม่ได้)
- ⬜ build ของ vml ตรวจ std ก่อนฝัง (std compile ไม่ผ่าน = build ไม่ผ่าน) → ขั้นแรกของการฝัง std เป็น bytecode
- ⬜ ตัวเลขทศนิยม → text ยังเป็น fixed 2 ตำแหน่งและล้นเมื่อค่าใหญ่ (`1e20`) → JSON number ยัง round-trip ไม่ตรง

## 10. Embed API (`vml.h`) — Module Registry แบบเดียว สร้างได้ 3 ทาง

หัวใจ: **ภายใน VM มี "module" แบบเดียว** (type + function + const + struct/enum)
ไม่ว่าจะมาจาก `.vml`, จาก C หรือสร้างผ่าน API ล้วนๆ ก็กลายเป็นของชนิดเดียวกัน
สคริปต์ `import` ได้เหมือนกันหมด และมองไม่เห็นว่ามาจากทางไหน

```
          .vml source ──┐
   .vmb (bytecode) ─────┼──►  Module Registry  ──►  import x  (เหมือนกันทุกตัว)
   C native function ───┤       (ใน VML_State)
   Builder API (C) ─────┘
```

### ทาง A — เขียนเป็น `.vml` แล้วฝังลง binary
```c
// build time:  vmlc std/json.vml -o json.vmb   (หรือ vmlc --embed → json_vmb.c)
extern const unsigned char json_vmb[];
extern const unsigned int  json_vmb_len;

vml_module_load_bytecode(L, "json", json_vmb, json_vmb_len);   // ไม่ต้อง parse ตอนรัน
vml_module_load_source(L, "util", util_src, util_len);         // หรือโหลดจาก source ตรงๆ
```

### ทาง B — register C function
```c
static VML_Result host_spawn(VML_Call *c) {
    const char *name = vml_arg_string(c, 0);
    int64_t hp       = vml_arg_int(c, 1);
    return vml_return_handle(c, spawn(name, hp));
}

VML_Module *m = vml_module_new(L, "game");
vml_module_type(m, "Entity", VML_OPAQUE);                       // type ที่ host ถือ pointer
vml_module_func(m, "spawn", "(name: string, hp: int): Entity", host_spawn);
vml_module_const_int(m, "MAX_PLAYERS", 16);
vml_module_finish(m);                                            // ตรวจ signature ทั้งหมดที่นี่
```

### ทาง C — Builder API: สร้าง function/struct ลง VM โดยไม่เขียน `.vml` และไม่ใช้ C callback
ใช้สร้าง binding อัตโนมัติ, code generator, หรือเครื่องมือภายนอก — ได้ bytecode จริงเร็วเท่า `.vml`
```c
VML_Module *m = vml_module_new(L, "mathx");

VML_StructB *v = vml_struct_begin(m, "Vec2");
vml_struct_field(v, "x", "float");
vml_struct_field(v, "y", "float");
vml_struct_end(v);

VML_FuncB *f = vml_func_begin(m, "lerp", "(a: float, b: float, t: float): float");
VML_Val a = vml_b_param(f, 0), b = vml_b_param(f, 1), t = vml_b_param(f, 2);
vml_b_return(f, vml_b_add(f, a, vml_b_mul(f, vml_b_sub(f, b, a), t)));   // a + (b - a) * t
vml_func_end(f);                                                          // type-check + emit bytecode

vml_module_finish(m);
```
- builder ทำงานระดับ **expression / statement ที่มี type** (ไม่ใช่เขียน opcode ดิบ)
  → เปลี่ยน opcode ภายในได้ทุกเมื่อโดย API ไม่พัง
- ผ่าน type checker ตัวเดียวกับ compiler → error เหมือนกัน
- ทาง A, B, C ผสมใน module เดียวกันได้ (เช่น struct จาก builder + method จาก `.vml` + I/O จาก C)

### สถานะ (✅ = ทำแล้ว — `include/vml.h`, `examples/embed/host.c`, `make embed-test`)
- ✅ ทาง A: `vml_module_source` (source ฝังในโปรแกรม) และ `.vmb` ทั้งโปรแกรม: `vml build app.vml -o app.vmb` / `vml app.vmb` / `vml_save_image` / `vml_load_image`
- ✅ ทาง B: `vml_module_new/type/func/const_*/finish` + `vml_arg_*` / `vml_return_*` / `vml_error` (error จับได้ด้วย `try`)
- ✅ `vml_call` / `vml_run_main` / `vml_last_error`, fuel ต่อการเรียก, ตั้งค่า GC, memory stats, print callback
- ✅ ทาง C: Builder API — `vml_struct_*`, `vml_enum_*`, `vml_func_begin` + `vml_b_*` (ค่า, operator, field, call, method, var, if/else/while, return)
  - ทุกคำสั่ง builder สร้างเป็นประกาศ `.vml` แล้ว compile ด้วย compiler ตัวเดียวกัน → type check เดียวกัน, bytecode เดียวกัน, เร็วเท่ากัน
  - ฟังก์ชัน C รับ struct ของ builder ได้ (field ส่งมาเรียงกัน: `(v: Vec2)` → `vml_arg_float(c, 0)` = v.x)
  - error ใน builder ถูกจับตอนสร้าง (ชื่อ/type ผิด) หรือตอนโหลด (type ไม่ตรง) พร้อมชื่อ module
- ✅ `.vmb` ทั้งโปรแกรมให้ผลเหมือนรันจาก source: ทั้ง 21 test ได้ output ตรงกันทุกบรรทัด (รวม std module ที่ import และ intrinsic native ที่ bind กลับด้วยชื่อ)
- ✅ interface อยู่ใน `.vmb` เอง (ส่วนแรกของไฟล์) — ประกาศ pub ทั้งหมดแบบไม่มี body; อ่านได้โดยไม่โหลด code: `vml interface x.vmb`, `vml_image_interface()`
- ⬜ `.vmb` ต่อ module + section แยก (code / constants / strings / debug) + โหมด bundle รวมไฟล์เดียวตอนแจก
- ⬜ `.vmi` เป็นแค่ทางเลือก: `vml interface x.vmb > x.vmi` สำหรับแจก type อย่างเดียว (และให้ compiler import จาก `.vmi` ได้)
- ⬜ หลายโปรแกรมต่อ state (ตอนนี้ 1 state = 1 โปรแกรม), print callback ยังเป็น global

### กฎความเสถียร (ABI)
- ⬜ host เห็นแค่ handle แบบ opaque: `VML_State`, `VML_Module`, `VML_Call`, `VML_Val` — ไม่เห็น struct ภายใน
- ⬜ ชื่อ `vml_<สิ่งของ>_<การกระทำ>` ทั้งหมด, ทุกฟังก์ชันคืน `VML_Result`
- ⬜ type ส่งเป็น **string signature ภาษาเดียวกับสคริปต์** — เพิ่ม type ใหม่ในภาษาได้โดย API ไม่ต้องเปลี่ยน
- ⬜ `VML_API_VERSION` แบบ semver: major เดิม = โค้ด host เดิม compile + รันได้เสมอ
- ⬜ bytecode `.vmb` มี header: magic, format version, hash ของ interface ที่ import
  → โหลด `.vmb` ที่ไม่เข้ากันจะได้ error ชัดเจน ไม่ crash
- 🟡 interface ของโปรแกรม: ✅ (`vml_interface`) — ของ host module: ยังดูได้แค่ `vml_module_generated_source`
- ⬜ ฟังก์ชันจากทาง B ถูกเรียกด้วย calling convention เดียวกับฟังก์ชัน VM — ไม่มีโหมดพิเศษ
- ⬜ allocator / output / fuel / memory limit ตั้งต่อ `VML_State`, หลาย state พร้อมกันได้

## 11. Memory

- ✅ GC แบบ mark-sweep (array, map, string, cell, closure) — ทำงานที่ safepoint (loop / call)
  - register และ cell ที่ไม่รู้ type สแกนแบบ conservative, container ที่รู้ type สแกนแม่นยำ
  - id ของ object ไม่เปลี่ยน (ใช้ free list), compact เฉพาะ byte ของ string
  - `--gc-stress` (เก็บทุก safepoint) / `--no-gc` สำหรับทดสอบ, `debug.gc()` / `debug.gc_count()`
- ✅ ตัดระบบ "คืน string ตอนออกจากฟังก์ชัน" ออก (เคยทำให้ string ที่เก็บไว้ใน array เสียหาย)
- ⬜ incremental / generational GC เพื่อลด pause ในเกม
- ⬜ ตั้งค่า threshold ผ่าน `vml.h`

## 12. ความเร็ว — ทุกขั้นตอน ไม่ใช่แค่ตอนรัน

### เป้าหมาย (วัดบนเครื่อง dev, build `-O3`)

| ขั้นตอน | เป้า |
|---|---|
| เปิด VM เปล่า (`vml_new` + โหลด stdlib จาก `.vmb`) | < 1 ms |
| lexer + parser → AST | ≥ 1 ล้านบรรทัด/วินาที (100k บรรทัด < 0.1 s) |
| type check + codegen → bytecode | ≥ 1 ล้านบรรทัด/วินาที |
| โหลด `.vmb` ที่ build ไว้แล้ว | ≥ 100 MB/วินาที (ไม่ต้อง parse เลย) |
| รันงานทั่วไป 1M รอบ (loop, call, string, array, map, float) | < 0.1 s ทุกตัว |
| `coroutine.resume` / `task.spawn` 1M ครั้ง | < 0.1 s |
| GC pause | < 1 ms ต่อรอบ (incremental) |
| `vml_call` จาก host 1M ครั้ง | < 0.1 s |

### Compiler
- ⬜ **Lexer**: อ่าน byte ตรงๆ ด้วยตาราง lookup 256 ช่อง, ไม่ copy string (token = offset + length), keyword ด้วย perfect hash
- ⬜ **AST**: เก็บใน arena เดียว, node อ้างกันด้วย index 32-bit (ไม่ใช่ pointer) → เล็ก, cache-friendly, free ทีเดียวทั้งก้อน
- ⬜ **ชื่อทุกชื่อ intern ครั้งเดียว** → เทียบชื่อ = เทียบเลข, scope lookup ด้วย hash ไม่ใช่วน array
- ⬜ **ไม่ย้อน parse**: lookahead ไม่เกิน 2 token, ไม่ copy lexer ไป scan ล่วงหน้าแบบตอนนี้
- ⬜ **Type check + codegen ใน pass เดียวบน AST** (แต่ละ function ทีละตัว)
- ⬜ **Register allocation แบบเส้นตรง** + ลด MOVE + ค่าคงที่เป็น operand ระหว่าง codegen
- ⬜ **Compile เฉพาะที่ใช้**: function ใน module ที่ไม่ถูกเรียกไม่ต้อง codegen (lazy)
- ⬜ **Cache**: `.vmb` ของ module ที่ไม่เปลี่ยน (เช็กด้วย hash ของ source) → build ซ้ำแทบไม่เสียเวลา
- ⬜ error recovery: เจอ error แล้ว parse ต่อได้ ไม่ต้องรันใหม่หลายรอบ

### Runtime
- ✅ typed opcode, register VM, computed goto, superinstruction
- ⬜ call frame เล็กที่สุด, ไม่แตะ state ที่ไม่จำเป็นตอน CALL/RET
- ⬜ struct = layout ตายตัว (index field ไม่ใช่ hash)
- ⬜ map = hash table จริง (ตอนนี้ยังวนหาแบบเส้นตรง)
- ⬜ string: hash เก็บไว้, intern เฉพาะค่าคงที่ (ทำแล้ว), slice ไม่ copy
- ⬜ fiber: stack แยกแบบโตได้, สลับ fiber = สลับ pointer ไม่ copy register

### กฎกันช้าลง
- ⬜ `make bench` วัดทุกเป้าในตารางบน → ถ้าตัวไหนช้าลงเกิน 10% จาก baseline = ถือว่าพัง เหมือน test ไม่ผ่าน
- ⬜ ทุก feature ใหม่ต้องมี benchmark ของตัวเองก่อน merge

## 13. สิ่งที่ตัดออก

- ✅ AOT / JIT
- ✅ `extern module`
- ✅ `pcall` / `xpcall` → แทนด้วยข้อ 5 (compiler ใหม่แจ้ง error พร้อมวิธีแก้)
- ⬜ `getenv` / `setenv` → แทนด้วยข้อ 8
- ✅ `nil` → แทนด้วย `T?`
- ⬜ capability `sys: System`

## 14. Roadmap สู่ 0.1 (ให้คนอื่นใช้แบบ standalone ได้)

### ตัวบล็อก — คนใช้เจอทันที
- 🟡 git + `.gitignore` (`git init` แล้ว ยังไม่ได้ commit แรก)
- ✅ ชื่อ module ซ้อนได้ไม่จำกัดชั้น (ข้อ 6)
- ✅ `std.os`: `args()`, `exit(code)` (catch หยุดไม่ได้); `vml app.vml a b`, `vml app.vml -- --flag`, `vml app.vmb a b`, `vml run -- a b`; embed: `vml_set_args`, `vml_exit_requested`
- ✅ script mode ไม่พิมพ์ banner เวลาอีกแล้ว (ใช้ `--time`)
- ✅ `io.eprint` / `io.ewrite` → stderr
- ✅ ตัวอย่าง CLI จริงเขียนด้วย vml: `examples/wc` (อ่านไฟล์จาก args, error ไป stderr, exit code) ผลตรงกับ `wc` ของ Unix
- ✅ ทศนิยม → text: ค่าที่สั้นที่สุดที่อ่านกลับได้ค่าเดิม (`0.1`, `1e+20`, `3.14159`; จำนวนเต็มไม่มี `.0`) ทั้ง `print`, f-string, `as string`, JSON
- ✅ literal ทศนิยมอ่านแบบแม่นยำ (เดิม `3.14159` → `3.1415900000000003`, `1e20` ล้น) + รองรับ `1e20`, `2.5e-3`
- ✅ path เต็มใน error (≤ 255 ตัว), ไฟล์ต่อโปรแกรม 32 → 128
- ⬜ ยกเพดานขนาดโปรแกรม: โค้ด 65,536 คำสั่ง, ฟังก์ชัน 2,048, struct 64, const 1,024, ไฟล์ 32, module 32 → โตได้ตามต้องการ
- ⬜ ชื่อ struct แยกตาม module (สอง package มี `Config` ได้)
- ⬜ ลำดับ import ใน module ไม่มีผล (ตอนนี้ `import "./writer"` ต้องอยู่ท้ายไฟล์)
- ⬜ f-string ที่มี `"` ใน `{...}` และ method call บน field ใน `{...}` (`{c.lines:pad_left(7)}`)
- ✅ `0.1 as f64` ได้ค่าตรง (literal สร้างเป็น f64 เลย ไม่ผ่าน f32) — ⬜ literal ทศนิยม default เป็น f32 ควรเป็น f64?
- ⬜ error บอกชื่อ type ภายใน (`expected 'i64'`) แทนชื่อที่ผู้ใช้เขียน (`int`)

### ความสามารถที่ประกาศไว้แต่ยังไม่มี
- ⬜ fiber → coroutine → task ที่ yield ได้ (ข้อ 7)
- ⬜ string แบบ UTF-8 (ความยาว/ตัดตามตัวอักษร ไม่ใช่ byte)
- ⬜ std: path, process, sort, format ทศนิยม, net

### แจกและใช้งาน
- ✅ `vml build` ได้โปรแกรมที่รันได้เลยเป็นค่า default (ไม่มี flag พิเศษ): project → `dist/<name>`, script → `./<name>`; ตั้งชื่อ `-o x.vmb` เมื่ออยากได้ bytecode image
  - ไฟล์ = vml runtime + `.vmb` + trailer 16 byte (`src/cli/exe.c`); ตอนเริ่ม vml ดูท้ายไฟล์ตัวเอง ถ้ามีโปรแกรม → รันเลย argument ทั้งหมดเป็นของโปรแกรม
  - ย้ายไฟล์ไปไหนก็รันได้ ไม่ต้องติดตั้ง vml; ⬜ runtime ที่ไม่มี compiler (ไฟล์เล็กลง ~600KB ตอนนี้), ⬜ Linux
- ⬜ (ไกล) compile เป็น native code (AOT ผ่าน C หรือ LLVM) — ข้อ 13 ตัด AOT/JIT ออกไว้ ต้องตัดสินใจใหม่
- ⬜ Linux (ตอนนี้ macOS เท่านั้น), Windows ภายหลัง
- ⬜ `make install` + release binary + CI รัน `make test` ทุก push
- ⬜ version ของภาษา + format `.vmb` (กฎ ABI ข้อ 10)

### เครื่องมือและเอกสาร
- ✅ เว็บ + playground: `web/` (หน้าเดียว static) — compiler + VM ตัวเดียวกับ `vml` compile เป็น WebAssembly (`make web`: LLVM clang + lld + wasi-libc) ขนาด ~480KB; shim WASI เล็กๆ ใน `web/vml-web.js` (stdout/stderr, ไม่มี fs/network, fuel กันลูปไม่จบ); ตัวอย่างทุกตัวถูกรันตรวจโดย `tools/check_web_examples.js`
  - ขึ้น GitHub Pages / static host ไหนก็ได้ (`web/index.html` สร้างจาก `web/site.html` ด้วย `tools/build_site.py`); ดูในเครื่อง: `node tools/serve_web.js`
- ⬜ macro / literal ฝังภาษาอื่น (`json!{ ... }`) — หลัง 0.1
- ⬜ `vml fmt`, `vml-analyzer` (LSP), REPL, debugger
- ⬜ tutorial + reference + เอกสาร std (`vml doc` จาก `##`)

ลำดับ: git → ตัวบล็อก → Linux/install/CI → เอกสาร → ปล่อย 0.1 → fiber/UTF-8/analyzer/git deps

---

## ลำดับการทำ

0. `make bench` + baseline (เพื่อวัดทุกขั้นตอนต่อจากนี้)
1. Compiler: lexer เร็ว → parser → AST arena → codegen (ฐานของทุกอย่าง)
2. Closure แบบเต็ม + type ฟังก์ชัน
3. Syntax เรียกฟังก์ชันใหม่ (ข้อ 2)
4. `T?`, `enum` + `match`, `int` = i64 ✅
5. Error แบบใหม่ (ข้อ 5) ✅
6. Module registry + `vml.h` ทาง A/B (ข้อ 6, 10)
6b. Builder API ทาง C (ข้อ 10) — ทำหลัง AST เพราะใช้ type checker ตัวเดียวกัน ✅
7. Intrinsic + stdlib เขียนด้วย `.vml` (ข้อ 9) ✅ (ส่วนหลัก)
8. fiber ใน VM → `coroutine` → `task` (ข้อ 7)
9. GC (ข้อ 11)
10. ลดคำสั่งใน compiler (ข้อ 12)
