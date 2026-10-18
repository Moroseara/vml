/* `.vmb` images: build one from a script, print its interface, run it */
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
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "cli.h"

/* `vml build <in.vml> -o <out.vmb>`: compile once, run later without the compiler */
int cli_build_script(int argc, char **argv) {
    const char *in = VM_NULL, *out = VM_NULL;
    const char *lib_path = VM_NULL;
    const char *pkgs[8];
    int num_pkgs = 0;
    for (int i = 2; i < argc; i++) {
        if (vm_str_eq_cstr(argv[i], vm_str_len(argv[i]), "-o") == VM_TRUE && i + 1 < argc) out = argv[++i];
        else if (vm_str_eq_cstr(argv[i], vm_str_len(argv[i]), "--lib-path") == VM_TRUE && i + 1 < argc) lib_path = argv[++i];
        else if (vm_str_eq_cstr(argv[i], vm_str_len(argv[i]), "--pkg") == VM_TRUE && i + 1 < argc) { if (num_pkgs < 8) pkgs[num_pkgs++] = argv[i + 1]; i++; }
        else in = argv[i];
    }
    if (in == VM_NULL) {
        vm_sys_write_cstr("Usage: vml build <script.vml> [-o file] [--lib-path dir] [--pkg name=dir]\n");
        return 1;
    }
    /* Default output: the script's name without .vml, as a program you run directly */
    char default_out[512];
    if (out == VM_NULL) {
        const char *base = strrchr(in, '/');
        base = base ? base + 1 : in;
        size_t bl = strlen(base);
        if (bl > 4 && strcmp(base + bl - 4, ".vml") == 0) bl -= 4;
        snprintf(default_out, sizeof(default_out), "%.*s", (int)bl, base);
        out = default_out;
    }
    size_t olen = strlen(out);
    int want_image = olen > 4 && strcmp(out + olen - 4, ".vmb") == 0;
    char image_path[600];
    snprintf(image_path, sizeof(image_path), "%s%s", out, want_image ? "" : ".vmb.tmp");
    VML_State *L = vml_new(VM_NULL);
    if (lib_path != VM_NULL) vml_set_lib_path(L, lib_path);
    for (int k = 0; k < num_pkgs; k++) {
        char pname[32];
        vm_u32 slen = vm_str_len(pkgs[k]), eq = 0;
        while (eq < slen && pkgs[k][eq] != '=') eq++;
        if (eq >= slen) continue;
        vm_str_copy(pname, pkgs[k], eq, 32);
        vml_mount_package(L, pname, &pkgs[k][eq + 1]);
    }
    if (vml_load_file(L, in) != VML_OK) {
        printf("%s\n", vml_last_error(L));
        vml_free(L);
        return 1;
    }
    if (vml_has_function(L, "main") == 0) {
        printf("[Compile Error] Missing entry point: please define 'func main()'\n");
        vml_free(L);
        return 1;
    }
    VML_Result r = vml_save_image_file(L, image_path);
    if (r != VML_OK) printf("%s\n", vml_last_error(L));
    vml_free(L);
    if (r != VML_OK) return 1;
    if (!want_image) {
        int ok = cli_write_executable(image_path, out, argv[0]);
        remove(image_path);
        if (!ok) return 1;
    }
    return 0;
}

/* `vml interface <file.vmb | file.vml>`: prints the public declarations */
int cli_interface(int argc, char **argv) {
    if (argc < 3) {
        vm_sys_write_cstr("Usage: ./vml interface <image.vmb | script.vml>\n");
        return 1;
    }
    const char *path = argv[2];
    FILE *f = fopen(path, "rb");
    if (f == VM_NULL) {
        printf("cannot open %s\n", path);
        return 1;
    }
    unsigned char head[16];
    size_t n = fread(head, 1, sizeof(head), f);
    if (n == 16 && head[0] == 'V' && head[1] == 'M' && head[2] == 'L' && head[3] == 'B') {
        /* Header only: read 16 bytes + the interface, never the code */
        size_t ilen = (size_t)head[12] | ((size_t)head[13] << 8) | ((size_t)head[14] << 16) | ((size_t)head[15] << 24);
        unsigned char *buf = (unsigned char *)malloc(16 + ilen);
        if (buf == VM_NULL) { fclose(f); return 1; }
        for (int i = 0; i < 16; i++) buf[i] = head[i];
        size_t got = fread(buf + 16, 1, ilen, f);
        fclose(f);
        size_t out_len = 0;
        const char *text = vml_image_interface(buf, 16 + got, &out_len);
        if (text == VM_NULL) {
            printf("not a compatible image: %s\n", path);
            free(buf);
            return 1;
        }
        fwrite(text, 1, out_len, stdout);
        free(buf);
        return 0;
    }
    fclose(f);
    VML_State *L = vml_new(VM_NULL);
    if (vml_load_file(L, path) != VML_OK) {
        printf("%s\n", vml_last_error(L));
        vml_free(L);
        return 1;
    }
    size_t out_len = 0;
    const char *text = vml_interface(L, &out_len);
    fwrite(text, 1, out_len, stdout);
    vml_free(L);
    return 0;
}

int cli_is_image_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == VM_NULL) return VM_FALSE;
    unsigned char m[4] = {0, 0, 0, 0};
    size_t n = fread(m, 1, 4, f);
    fclose(f);
    return vm_bool_from_int(n == 4 && m[0] == 'V' && m[1] == 'M' && m[2] == 'L' && m[3] == 'B');
}

/* Runs a precompiled `.vmb` image */
int cli_run_image(const char *path, int argc, const char *const *argv) {
    VML_State *L = vml_new(VM_NULL);
    vml_set_args(L, argc, argv);
    VML_Result r = vml_load_image_file(L, path);
    if (r == VML_OK) r = vml_run_main(L);
    int code = 0;
    if (r == VML_OK && vml_exit_requested(L, &code)) {
        vml_free(L);
        return code;
    }
    if (r != VML_OK) {
        printf("[Runtime Error] %s\n", vml_last_error(L));
        vml_free(L);
        return 1;
    }
    vml_free(L);
    return 0;
}

