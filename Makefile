CC = clang
CFLAGS = -O3 -Wall -Wextra -std=c11
DEV_CFLAGS = -O1 -g -Wall -Wextra -std=c11 -DVM_DEV_MODE=1

VM_SRC = vm/string/vm_string.c \
         vm/memory/vm_memory.c \
         vm/syscall/vm_syscall.c \
         vm/cpu/vm_cpu.c \
         vm/disasm/vm_disasm.c \
         vm/stdlib/std_math.c \
         vm/stdlib/std_string.c \
         vm/stdlib/std_bit.c \
         vm/stdlib/std_io.c \
         vm/stdlib/std_array.c \
         vm/stdlib/std_map.c \
         vm/stdlib/std_task.c \
         vm/stdlib/vm_intrinsics.c \
         vm/embed/vm_embed.c \
         vm/opt/vm_fuse.c \
         vm/gc/vm_gc.c \
         vm/api/vml.c \
         vm/api/vml_image.c \
         vm/api/vml_builder.c

COMPILER_SRC = compiler/lexer/lexer_char.c \
               compiler/lexer/lexer.c \
               compiler/scope/scope_var.c \
               compiler/scope/scope_func.c \
               compiler/scope/scope_type.c \
               compiler/module/module_import.c \
               compiler/module/module_export.c \
               compiler/expr/expr_lambda.c \
               compiler/expr/expr_call.c \
               compiler/expr/expr_primary.c \
               compiler/expr/expr_ops.c \
               compiler/stmt/stmt_branch.c \
               compiler/stmt/stmt_loop.c \
               compiler/stmt/stmt_var.c \
               compiler/stmt/stmt_assign.c \
               compiler/decl/decl_func.c \
               compiler/decl/decl_struct.c \
               compiler/decl/decl_generic.c \
               compiler/compiler.c \
               compiler/ast/ast.c \
               compiler/ast/parser.c \
               compiler/gen/gen_util.c \
               compiler/gen/gen_expr.c \
               compiler/gen/gen_stmt.c \
               compiler/gen/gen_decl.c \
               compiler/gen/gen_closure.c \
               compiler/gen/gen_optional.c \
               compiler/gen/gen_error.c \
               compiler/gen/gen_enum.c \
               compiler/gen/gen_interface.c \
               build/std_sources.c

# The std tree (its tests are not embedded)
STD_VML = $(shell find std -name '*.vml' -not -path 'std/tests/*' | sort)

# The `vml` command line (SPEC 6c): one front door, one file per command group
MAIN_SRC = src/cli/main.c src/cli/script.c src/cli/image.c src/cli/project.c

OUT = vml

# libvml = VM + compiler + loader. vml, embed_host and future tools (vml-analyzer) link it.
LIB_SRC = $(VM_SRC) $(COMPILER_SRC)
LIB = build/libvml.a
LIB_OBJ = $(patsubst %.c,build/obj/%.o,$(LIB_SRC))
LIB_REL = build/libvml-release.a
LIB_REL_OBJ = $(patsubst %.c,build/obj-release/%.o,$(LIB_SRC))

.PHONY: all dev release clean test bench bench-baseline embed-test

all: $(OUT)

build/std_sources.c: tools/embed_std.c $(STD_VML)
	@mkdir -p build
	@$(CC) -O1 tools/embed_std.c -o build/embed_std
	@./build/embed_std $@ $(STD_VML)

build/obj/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/obj-release/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -DNDEBUG -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJ)
	@rm -f $@
	@ar rcs $@ $^

$(LIB_REL): $(LIB_REL_OBJ)
	@rm -f $@
	@ar rcs $@ $^

-include $(LIB_OBJ:.o=.d) $(LIB_REL_OBJ:.o=.d)

$(OUT): $(LIB) $(MAIN_SRC) src/cli/cli.h include/vml.h
	$(CC) $(CFLAGS) $(MAIN_SRC) $(LIB) -o $(OUT)

dev: build/std_sources.c
	$(CC) $(DEV_CFLAGS) $(LIB_SRC) $(MAIN_SRC) -o $(OUT)

release: build/std_sources.c $(LIB_REL)
	$(CC) $(CFLAGS) -DNDEBUG $(MAIN_SRC) $(LIB_REL) -o $(OUT)

embed-test: $(LIB) examples/embed/host.c include/vml.h
	$(CC) $(CFLAGS) examples/embed/host.c $(LIB) -o embed_host
	@./embed_host

bench: release
	@bench/run.sh

bench-baseline: release
	@bench/run.sh --save

clean:
	rm -rf $(OUT) embed_host build

test: $(OUT)
	@echo "=== Running VM Test Suite ==="
	@./$(OUT) tests/01_basics.vml
	@./$(OUT) tests/02_strings.vml
	@./$(OUT) tests/03_structs.vml
	@./$(OUT) tests/04_contracts.vml
	@./$(OUT) tests/05_metamethods.vml
	@./$(OUT) tests/06_nested_funcs.vml
	@./$(OUT) tests/07_diagnostics.vml
	@./$(OUT) tests/08_docs.vml
	@./$(OUT) tests/09_std_modules.vml
	@./$(OUT) tests/10_header_type_system.vml
	@./$(OUT) tests/11_arrays_maps_tasks.vml
	@./$(OUT) tests/12_error_handling.vml
	@./$(OUT) tests/13_heap_growth.vml
	@./$(OUT) tests/14_closures.vml
	@./$(OUT) tests/15_gc.vml
	@./$(OUT) tests/16_call_syntax.vml
	@./$(OUT) tests/17_types.vml
	@./$(OUT) tests/18_errors.vml
	@./$(OUT) tests/19_stdlib.vml
	@./$(OUT) tests/20_generics.vml
	@./$(OUT) tests/21_std_modules2.vml
	@./$(OUT) tests/22_modules.vml --lib-path tests/pkgs
	@./$(OUT) tests/23_relative_import.vml
	@./$(OUT) tests/24_numbers.vml
	@tools/run_fail_tests.sh
	@tools/test_project.sh
	@./$(OUT) test
	@echo "============================="
	@echo "All Automated Tests PASSED! ✓"
	@echo "============================="
