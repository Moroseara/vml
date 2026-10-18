#!/bin/sh
# Project system tests (SPEC 6b): vml new/run/build/test/clean, path deps, build cache.
# Works in a fresh temp folder outside the repo (the repo root is itself a workspace).
cd "$(dirname "$0")/.."
VM="$PWD/vml"
W=$(mktemp -d "${TMPDIR:-/tmp}/vml_projtest.XXXXXX")
fail=0
check() {   # check <name> <command output> <expected substring>
    if printf '%s' "$2" | grep -qF -- "$3"; then :; else
        echo "FAIL project/$1"; echo "  want: $3"; echo "  got:  $(printf '%s' "$2" | head -3)"; fail=1
    fi
}

cd "$W"
out=$("$VM" new game 2>&1);                       check new "$out" "created game/"
out=$("$VM" new game 2>&1);                       check new_exists "$out" "already exists"
out=$("$VM" new std 2>&1);                        check new_std "$out" "not 'std'"
cd game
out=$("$VM" run -v 2>&1);                         check run_first "$out" "vml: compiled"
check run_output "$out" "Hello, world!"
out=$("$VM" run -v 2>&1);                         check run_cached "$out" "up to date (cached build)"
out=$(cd src && "$VM" run -v 2>&1);               check run_from_subfolder "$out" "up to date (cached build)"
out=$("$VM" test 2>&1);                           check test_cmd "$out" "1/1 test files passed"
out=$("$VM" build 2>&1);                          check build_cmd "$out" "built dist/game"
out=$(./dist/game 2>&1);                          check build_program_runs "$out" "Hello, world!"

# Any source change rebuilds
sed 's/Hello, /Hi, /' src/greet.vml > src/g.tmp && mv src/g.tmp src/greet.vml
out=$("$VM" run -v 2>&1);                         check edit_rebuilds "$out" "vml: compiled"
check edit_output "$out" "Hi, world!"
out=$("$VM" test 2>&1);                           check test_catches_change "$out" "0/1 test files passed"
sed 's/Hi, /Hello, /' src/greet.vml > src/g.tmp && mv src/g.tmp src/greet.vml

# A broken edit fails and does not leave a key pointing at the old image
cp src/greet.vml src/greet.bak
printf 'module game.greet\npub func hello(who: string): string { return 1 }\n' > src/greet.vml
out=$("$VM" run -v 2>&1);                         check broken_fails "$out" "Type mismatch"
out=$("$VM" run -v 2>&1);                         check broken_not_cached "$out" "Type mismatch"
mv src/greet.bak src/greet.vml

# Path dependency
mkdir -p ../mathx/src
printf '[project]\nname = "mathx"\nversion = "1.0.0"\n' > ../mathx/vml.toml
printf 'module mathx.ops\n\nfunc twice(x: int): int { return x * 2 }\npub func quad(x: int): int { return twice(twice(x)) }\n' > ../mathx/src/ops.vml
printf '[project]\nname = "game"   # the root of our modules\nentry = "src/main.vml"\n\n[deps]\nmathx = { path = "../mathx" }\n' > vml.toml
printf 'import game.greet\nimport mathx.ops\n\nfunc main() {\n    print(greet.hello("dep"))\n    print(ops.quad(3))\n}\n' > src/main.vml
out=$("$VM" run 2>&1);                            check dep_works "$out" "12"
printf 'module mathx.ops\n\npub func quad(x: int): int { return x * 100 }\n' > ../mathx/src/ops.vml
out=$("$VM" run -v 2>&1);                         check dep_edit_rebuilds "$out" "300"
printf 'import mathx.ops\nfunc main() { assert(ops.quad(1) == 100, "dep in tests") }\n' > tests/dep_test.vml
out=$("$VM" test 2>&1);                           check dep_in_tests "$out" "2/2 test files passed"

# A dependency cannot declare modules under another package's name
printf 'module game.stolen\npub func f(): int { return 1 }\n' > ../mathx/src/steal.vml
printf 'import mathx.steal\nfunc main() { print(stolen.f()) }\n' > tests/steal_test.vml
out=$("$VM" test 2>&1);                           check dep_impersonation "$out" "must be under 'mathx'"
rm tests/steal_test.vml ../mathx/src/steal.vml

# Manifest errors
cp vml.toml vml.good
printf '[project]\nname = "game"\n[deps]\nmathx = { path = "../mathx", as = "mx" }\n' > vml.toml
out=$("$VM" run 2>&1);                            check dep_as_later "$out" "renaming a dependency"
printf '[project]\nname = "game"\n[deps]\nnet = { git = "https://example.com/net" }\n' > vml.toml
out=$("$VM" run 2>&1);                            check dep_git_later "$out" "git dependencies are not supported yet"
printf '[project]\nname = "game"\n[deps]\nwrongname = { path = "../mathx" }\n' > vml.toml
out=$("$VM" run 2>&1);                            check dep_name_mismatch "$out" "is named 'mathx' in its vml.toml"
printf '[project]\nname = "game"\n[deps]\nnope = { path = "../nope" }\n' > vml.toml
out=$("$VM" run 2>&1);                            check dep_missing "$out" "no vml.toml in"
printf '[project]\nname = game\n' > vml.toml
out=$("$VM" run 2>&1);                            check toml_bad_value "$out" "must be a \"string\""
printf '[project]\nversion = "1"\n' > vml.toml
out=$("$VM" run 2>&1);                            check toml_no_name "$out" "needs name"
mv vml.good vml.toml

out=$("$VM" clean 2>&1);                          check clean "$out" "removed .vml-cache"
[ -d .vml-cache ] && { echo "FAIL project/clean_removes"; fail=1; }
out=$("$VM" run -v 2>&1);                         check after_clean "$out" "vml: compiled"

out=$(cd "$W" && "$VM" run 2>&1);                 check no_project "$out" "no vml.toml here"

# Module as a folder inside a project: src/ui/mod.vml + internal file
mkdir -p src/ui
printf 'module game.ui\n\nimport "./widgets"\n\npub func title(): string { return frame("menu") }\n' > src/ui/mod.vml
printf 'func frame(s: string): string { return "[" + s + "]" }\n' > src/ui/widgets.vml
printf 'import game.ui\nfunc main() { print(ui.title()) }\n' > src/main.vml
out=$("$VM" run 2>&1);                            check folder_module "$out" "[menu]"

# Source root anywhere: no src/ folder, modules next to vml.toml
mkdir -p "$W/flat" && cd "$W/flat"
printf '[project]\nname = "flat"\n' > vml.toml
printf 'import flat.util\nfunc main() { print(util.n()) }\n' > main.vml
printf 'module flat.util\npub func n(): int { return 7 }\n' > util.vml
out=$("$VM" run 2>&1);                            check no_src_folder "$out" "7"
mkdir -p code && mv util.vml code/ && printf 'import flat.util\nfunc main() { print(util.n() * 2) }\n' > code/main.vml && rm main.vml
printf '[project]\nname = "flat"\nsource = "code"\n' > vml.toml
out=$("$VM" run 2>&1);                            check source_folder "$out" "14"
printf '[project]\nname = "flat"\nsource = "../elsewhere"\n' > vml.toml
out=$("$VM" run 2>&1);                            check source_outside "$out" "must be a folder inside the project"

# Ignore rules: ignored files do not affect the cache; include overrides
printf '[project]\nname = "flat"\nsource = "code"\nexclude = ["scratch/"]\n' > vml.toml
mkdir -p code/scratch code/gen
out=$("$VM" run -v 2>&1);                         check ign_first "$out" "vml: compiled"
printf 'func junk() {}\n' > code/scratch/a.vml
out=$("$VM" run -v 2>&1);                         check ign_exclude "$out" "up to date"
printf 'gen/\n' > code/.gitignore
printf 'func g() {}\n' > code/gen/b.vml
out=$("$VM" run -v 2>&1);                         check ign_gitignore "$out" "up to date"
printf '[project]\nname = "flat"\nsource = "code"\nexclude = ["scratch/"]\ninclude = ["gen/"]\n' > vml.toml
out=$("$VM" run -v 2>&1);                         check ign_include_rebuild "$out" "vml: compiled"
printf 'func g2() {}\n' > code/gen/b.vml
out=$("$VM" run -v 2>&1);                         check ign_include_tracks "$out" "vml: compiled"
printf 'func z() {}\n' > code/temp.vml
out=$("$VM" run -v 2>&1);                         check ign_new_file "$out" "vml: compiled"
printf 'temp.vml\n' > code/.vmlignore
out=$("$VM" run -v 2>&1);                         check ign_vmlignore "$out" "vml: compiled"
printf 'func z2() {}\n' > code/temp.vml
out=$("$VM" run -v 2>&1);                         check ign_vmlignore_skip "$out" "up to date"

# The name std is only for the built-in std
printf '[project]\nname = "std"\n' > vml.toml
out=$("$VM" run 2>&1);                            check std_name_reserved "$out" "only the built-in std may be named 'std'"

# Workspace: members, a workspace dependency, -p, and tests across members
mkdir -p "$W/ws/app/src" "$W/ws/app/tests" "$W/ws/libs/util/src" && cd "$W/ws"
printf '[workspace]\nmembers = ["app", "libs/*"]\n\n[workspace.deps]\nutil = { path = "libs/util" }\n' > vml.toml
printf '[project]\nname = "util"\n' > libs/util/vml.toml
printf 'module util.calc\npub func add(a: int, b: int): int { return a + b }\n' > libs/util/src/calc.vml
printf '[project]\nname = "app"\n[deps]\nutil = { workspace = true }\n' > app/vml.toml
printf 'import util.calc\nfunc main() { print(calc.add(20, 22)) }\n' > app/src/main.vml
printf 'import util.calc\nfunc main() { assert(calc.add(1, 1) == 2, "add") }\n' > app/tests/add_test.vml
out=$("$VM" run 2>&1);                            check ws_root_needs_p "$out" "is a workspace: use -p"
out=$("$VM" run -p app 2>&1);                     check ws_run_p "$out" "42"
out=$(cd app && "$VM" run 2>&1);                  check ws_run_in_member "$out" "42"
out=$("$VM" test 2>&1);                           check ws_test_all "$out" "1/1 test files passed"
out=$("$VM" run -p nope 2>&1);                    check ws_bad_member "$out" "no workspace member named 'nope'"
printf '[project]\nname = "app"\n[deps]\nother = { workspace = true }\n' > app/vml.toml
out=$("$VM" run -p app 2>&1);                     check ws_dep_missing "$out" "not listed in [workspace.deps]"

# CLI front door: help, unknown command, external vml-<name> tools
out=$("$VM" help 2>&1);                           check cli_help "$out" "commands:"
out=$("$VM" help test 2>&1);                      check cli_help_cmd "$out" "usage: vml test"
out=$("$VM" frobnicate 2>&1); code=$?
check cli_unknown "$out" "unknown command 'frobnicate'"
[ $code -eq 2 ] || { echo "FAIL project/cli_unknown_exit (got $code)"; fail=1; }
mkdir -p "$W/bin" && printf '#!/bin/sh\necho "tool args: $*"\n' > "$W/bin/vml-greet" && chmod +x "$W/bin/vml-greet"
out=$(PATH="$W/bin:$PATH" "$VM" greet x y 2>&1); check cli_external "$out" "tool args: x y"

# Program arguments and exit codes: script, image, project
mkdir -p "$W/cli" && cd "$W/cli"
printf 'import std.os\nfunc stop(): int {\n    os.exit(3)\n    return 1\n}\nfunc main() {\n    let a = os.args()\n    print(a:join(","))\n    if a:len() > 0 && a[0] == "boom" { let v = stop() catch e { print("caught " + e); 0 } }\n    os.exit(a:len())\n}\n' > app.vml
out=$("$VM" app.vml x y z 2>&1); code=$?;        check args_script "$out" "x,y,z"
[ $code -eq 3 ] || { echo "FAIL project/exit_code_script (got $code)"; fail=1; }
out=$("$VM" app.vml -- --time -v 2>&1);           check args_after_dashdash "$out" "--time,-v"
out=$("$VM" app.vml boom 2>&1); code=$?
[ $code -eq 3 ] || { echo "FAIL project/exit_not_caught (got $code)"; fail=1; }
printf '%s' "$out" | grep -q caught && { echo "FAIL project/exit_not_caught (catch saw it)"; fail=1; }
"$VM" build app.vml -o app.vmb >/dev/null
out=$("$VM" app.vmb p q 2>&1); code=$?;          check args_image "$out" "p,q"
[ $code -eq 2 ] || { echo "FAIL project/exit_code_image (got $code)"; fail=1; }
out=$("$VM" app.vml 2>&1);                        printf '%s' "$out" | grep -q "Execute time" && { echo "FAIL project/no_timing_banner"; fail=1; }
"$VM" new clip >/dev/null && cd clip
printf 'import std.os\nfunc main() {\n    print(os.args():join("+"))\n    os.exit(5)\n}\n' > src/main.vml
out=$("$VM" run -- a b 2>&1); code=$?;            check args_project "$out" "a+b"
[ $code -eq 5 ] || { echo "FAIL project/exit_code_project (got $code)"; fail=1; }

# Built programs: `vml build` makes something you run directly; *.vmb only when asked
mkdir -p "$W/exe" && cd "$W/exe"
printf 'import std.os\nimport std.io\nfunc main() {\n    io.eprint("to stderr")\n    print("args=" + os.args():join(","))\n    os.exit(4)\n}\n' > tool.vml
"$VM" build tool.vml >/dev/null 2>&1;            [ -x tool ] || { echo "FAIL project/exe_default_name"; fail=1; }
out=$(./tool help run 2>/dev/null); code=$?;      check exe_args "$out" "args=help,run"
[ $code -eq 4 ] || { echo "FAIL project/exe_exit_code (got $code)"; fail=1; }
out=$(./tool 2>&1 >/dev/null);                    check exe_stderr "$out" "to stderr"
mkdir -p moved && cp tool moved/renamed && out=$(cd / && "$W/exe/moved/renamed" z 2>/dev/null); check exe_moved "$out" "args=z"
"$VM" build tool.vml -o tool.vmb >/dev/null 2>&1; out=$("$VM" tool.vmb q 2>/dev/null); check build_vmb_by_name "$out" "args=q"
"$VM" build tool.vml -o other >/dev/null 2>&1;    out=$(./other 2>/dev/null); check exe_named "$out" "args="
"$VM" new prog >/dev/null && cd prog
"$VM" build >/dev/null 2>&1;                      out=$(./dist/prog 2>&1); check project_exe "$out" "Hello, world!"
"$VM" build -o prog.vmb >/dev/null 2>&1;          out=$("$VM" prog.vmb 2>&1); check project_vmb "$out" "Hello, world!"

cd / && rm -rf "$W"
[ $fail -eq 0 ] && echo "Project tests: PASS" || { echo "Project tests: FAILED"; exit 1; }
