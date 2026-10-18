/* `vml file.vml [flags]`: compiles and runs one script (no project needed, like a C file) */
#include "../../vm/core/vm_types.h"
#include "../../vm/memory/vm_memory.h"
#include "../../vm/cpu/vm_cpu.h"
#include "../../vm/syscall/vm_syscall.h"
#include "../../vm/string/vm_string.h"
#include "../../vm/disasm/vm_disasm.h"
#include "../../vm/embed/vm_embed.h"
#include "../../compiler/compiler.h"
#include "../../compiler/ast/ast.h"
#include "../../compiler/gen/gen.h"
#include "../../include/vml.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cli.h"

extern int   open(const char *path, int flags, ...);
extern long  read(int fd, void *buf, unsigned long count);
extern int   close(int fd);
extern void *realloc(void *ptr, unsigned long size);
extern void  free(void *ptr);

static char   *source_buffer = VM_NULL;
static VMChunk global_chunk;
static VMCPU   global_cpu;

static vm_bool load_script_file(const char *path) {
    int fd = open(path, 0);
    if (fd < 0) {
        vm_sys_write_cstr("Error: Could not open script file: ");
        vm_sys_write_cstr(path);
        vm_sys_write_cstr("\n");
        return VM_FALSE;
    }
    /* Read the whole file, growing the buffer as needed (no fixed source size limit) */
    unsigned long cap = 65536, len = 0;
    char *buf = (char *)realloc(VM_NULL, cap);
    while (buf != VM_NULL) {
        if (len + 1 >= cap) {
            char *grown = (char *)realloc(buf, cap * 2);
            if (grown == VM_NULL) { free(buf); buf = VM_NULL; break; }
            buf = grown;
            cap *= 2;
        }
        long n = read(fd, buf + len, cap - len - 1);
        if (n < 0) { free(buf); buf = VM_NULL; break; }
        if (n == 0) break;
        len += (unsigned long)n;
    }
    close(fd);
    if (buf == VM_NULL) {
        vm_sys_write_cstr("Error: Could not read script file: ");
        vm_sys_write_cstr(path);
        vm_sys_write_cstr("\n");
        return VM_FALSE;
    }
    buf[len] = '\0';
    source_buffer = buf;
    return VM_TRUE;
}

static vm_f64 get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (vm_f64)ts.tv_sec + (vm_f64)ts.tv_nsec * 1e-9;
}

int cli_run_script(int argc, char **argv) {
    const char *path = argv[1];
    vm_bool show_disasm = VM_FALSE;
    vm_bool show_ast = VM_FALSE;
    vm_bool parse_only = VM_FALSE;
    vm_bool use_legacy = VM_FALSE;
    vm_bool gc_stress = VM_FALSE;
    vm_bool no_gc = VM_FALSE;
    VMConfig cfg;
    vm_config_init_default(&cfg);

    /* vml flags come first; anything else (and everything after `--`) is for the program: os.args() */
    static const char *prog_args[256];
    int num_prog_args = 0;
    vm_bool show_time = VM_FALSE;
    for (int i = 2; i < argc; i++) {
        vm_u32 alen = vm_str_len(argv[i]);
        if (vm_str_eq_cstr(argv[i], alen, "--") == VM_TRUE) {
            for (i++; i < argc && num_prog_args < 256; i++) prog_args[num_prog_args++] = argv[i];
            break;
        }
        if (vm_str_eq_cstr(argv[i], alen, "--time") == VM_TRUE)             show_time = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--disasm") == VM_TRUE)      show_disasm = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--ast") == VM_TRUE)          show_ast = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--parse-only") == VM_TRUE)   parse_only = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--legacy") == VM_TRUE)       use_legacy = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--gc-stress") == VM_TRUE)    gc_stress = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--no-gc") == VM_TRUE)        no_gc = VM_TRUE;
        else if (vm_str_eq_cstr(argv[i], alen, "--no-io") == VM_TRUE)        vm_config_disable(&cfg, VM_FEAT_STD_IO);
        else if (vm_str_eq_cstr(argv[i], alen, "--no-coroutine") == VM_TRUE) vm_config_disable(&cfg, VM_FEAT_COROUTINE);
        else if (vm_str_eq_cstr(argv[i], alen, "--no-random") == VM_TRUE)    vm_config_disable(&cfg, VM_FEAT_STD_RANDOM);
        else if (vm_str_eq_cstr(argv[i], alen, "--lib-path") == VM_TRUE && i + 1 < argc) {
            vm_config_set_lib_path(&cfg, argv[++i]);
        } else if (vm_str_eq_cstr(argv[i], alen, "--pkg") == VM_TRUE && i + 1 < argc) {
            const char *spec = argv[++i];
            vm_u32 slen = vm_str_len(spec);
            vm_u32 eq = 0;
            while (eq < slen && spec[eq] != '=') eq++;
            if (eq < slen) {
                char pname[32];
                vm_str_copy(pname, spec, eq, 32);
                vm_config_mount_package(&cfg, pname, &spec[eq + 1]);
            }
        } else if (num_prog_args < 256) {
            prog_args[num_prog_args++] = argv[i];
        }
    }

    if (load_script_file(path) == VM_FALSE) return 1;

    if (show_ast == VM_TRUE || parse_only == VM_TRUE) {
        /* New front end (AST). Not wired to codegen yet: parse, report, exit. */
        AstTree tree;
        ast_init(&tree, VM_NULL, VM_NULL);
        vm_f64 t0 = get_time_sec();
        vm_bool ok = ast_parse(&tree, source_buffer);
        vm_f64 t1 = get_time_sec();
        if (show_ast == VM_TRUE) ast_dump(&tree);
        ast_print_errors(&tree, path);
        vm_sys_write_cstr("[VM Kernel] Parse time (ms)   : ");
        printf("%.4f\n", (t1 - t0) * 1000.0); fflush(stdout);
        vm_sys_write_cstr("[VM Kernel] AST nodes         : ");
        vm_sys_print_i32((vm_i32)tree.num_nodes);
        ast_free(&tree);
        free(source_buffer);
        return (ok == VM_TRUE) ? 0 : 1;
    }

    vm_chunk_init(&global_chunk);
    (void)vm_chunk_set_file(&global_chunk, path);

    vm_f64 t_compile_start = get_time_sec();
    vm_bool compiled = (use_legacy == VM_TRUE)
                       ? vm_compile_source_legacy(source_buffer, &global_chunk, &cfg)
                       : vm_compile_source_with_config(source_buffer, &global_chunk, &cfg);
    if (compiled == VM_FALSE) {
        return 1;
    }
    vm_f64 t_compile_end = get_time_sec();

    if (show_disasm == VM_TRUE) {
        vm_disassemble_chunk(&global_chunk);
    }

    global_chunk.prog_args = prog_args;
    global_chunk.prog_argc = (vm_u32)num_prog_args;
    vm_cpu_init(&global_cpu);
    if (gc_stress == VM_TRUE) global_chunk.gc_threshold = 1;   /* collect at every safepoint */
    if (no_gc == VM_TRUE) global_chunk.gc_disabled = VM_TRUE;

    vm_f64 t_run_start = get_time_sec();
    (void)vm_cpu_execute(&global_cpu, &global_chunk);
    vm_bool run_failed = vm_bool_from_int(global_cpu.has_error && !global_chunk.exit_requested);
    if (global_chunk.exit_requested) {
        int code = global_chunk.exit_code;
        vm_chunk_free(&global_chunk);
        free(source_buffer);
        return code;
    }

    /* Optional Host -> Script Embed Call Demo (`on_host_tick(frame_no)`) */
    vm_i32 tick_fid = vm_embed_find_func(&global_chunk, "on_host_tick");
    if (tick_fid >= 0) {
        VMReg arg0;
        arg0.i32 = 60;
        VMReg host_ret = vm_embed_call_func(&global_cpu, &global_chunk, tick_fid, &arg0, 1);
        vm_sys_write_cstr("[Embed Host API] Called script on_host_tick(60) -> returned: ");
        vm_sys_print_i32(host_ret.i32);
    }

    vm_f64 t_run_end = get_time_sec();

    if (show_time == VM_TRUE) {
        vm_sys_write_cstr("\n-------------------------------------------------------------\n");
        vm_sys_write_cstr("[VM Kernel] Compile time (ms) : ");
        printf("%.4f\n", (t_compile_end - t_compile_start) * 1000.0); fflush(stdout);
        vm_sys_write_cstr("[VM Kernel] Execute time (sec): ");
        printf("%.4f\n", t_run_end - t_run_start); fflush(stdout);
        vm_sys_write_cstr("-------------------------------------------------------------\n");
    }

    vm_chunk_free(&global_chunk);
    free(source_buffer);
    return (run_failed == VM_TRUE) ? 1 : 0;
}
