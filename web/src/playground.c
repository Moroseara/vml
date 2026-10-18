/*
 * WebAssembly entry for the browser playground. The page calls:
 *   ptr = vml_web_alloc(n)      -> write UTF-8 source there
 *   code = vml_web_run(ptr)     -> compile + run; output arrives through WASI fd_write
 * Same compiler and VM as the vml binary (libvml), so results match the command line.
 */
#include "../../include/vml.h"
#include <stdio.h>
#include <stdlib.h>

__attribute__((export_name("vml_web_alloc")))
char *vml_web_alloc(int n) {
    return (char *)malloc((size_t)n + 1);
}

__attribute__((export_name("vml_web_free")))
void vml_web_free(char *p) {
    free(p);
}

static const char *const NO_ARGS[1] = {0};

/* 0 = ok, 1 = compile error, 2 = runtime error, otherwise the os.exit code + 100.
   deny: comma-separated module names the sandbox turns off ("std.fs,std.env"); gc_stress: collect at
   every safepoint (shows the GC keeps everything reachable alive). */
__attribute__((export_name("vml_web_run")))
int vml_web_run(const char *source, unsigned long long fuel, const char *deny, int gc_stress) {
    VML_Options opts;
    vml_options_default(&opts);
    opts.fuel = fuel;              /* stops runaway loops so the tab never freezes */
    if (gc_stress) opts.gc_threshold = 1;
    VML_State *L = vml_new(&opts);
    vml_set_args(L, 0, NO_ARGS);
    char name[64];
    for (const char *p = deny; p && *p;) {
        int n = 0;
        while (*p && *p != ',') { if (n < 63) name[n++] = *p; p++; }
        name[n] = '\0';
        if (n > 0) vml_deny_module(L, name);
        if (*p == ',') p++;
    }
    int code = 0;
    if (vml_load_source(L, "main.vml", source) != VML_OK) {
        printf("%s\n", vml_last_error(L));
        code = 1;
    } else if (!vml_has_function(L, "main")) {
        printf("[Compile Error] main.vml: missing entry point 'func main()'\n");
        code = 1;
    } else if (vml_run_main(L) != VML_OK) {
        printf("[Runtime Error] %s\n", vml_last_error(L));
        code = 2;
    } else {
        int ec = 0;
        if (vml_exit_requested(L, &ec)) code = 100 + ec;
    }
    fflush(stdout);
    fflush(stderr);
    vml_free(L);
    return code;
}

__attribute__((export_name("vml_web_version")))
const char *vml_web_version(void) {
    return vml_version();
}
