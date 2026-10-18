#ifndef VML_H
#define VML_H

/*
 * ============================================================================
 * vml.h — the embedding API
 *
 * One header, opaque handles, `vml_` names, and every fallible call returns a
 * VML_Result. Code written against VML_VERSION_MAJOR keeps compiling and
 * running as long as the major version does not change.
 *
 *   VML_State *L = vml_new(NULL);
 *
 *   VML_Module *m = vml_module_new(L, "game");
 *   vml_module_type(m, "Entity");                        // opaque handle type
 *   vml_module_func(m, "spawn", "(name: string, hp: int): Entity", host_spawn);
 *   vml_module_const_int(m, "MAX_PLAYERS", 16);
 *   vml_module_finish(m);
 *
 *   if (vml_load_source(L, "main.vml", src) != VML_OK) puts(vml_last_error(L));
 *   vml_call(L, "on_update", "f", 0.016);                // script: import game ... func on_update(dt: float)
 *
 *   vml_save_image_file(L, "main.vmb");                  // precompiled: load later without compiling
 *   vml_free(L);
 * ============================================================================
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VML_VERSION_MAJOR 0
#define VML_VERSION_MINOR 1
#define VML_VERSION_PATCH 0

typedef struct VML_State  VML_State;
typedef struct VML_Module VML_Module;
typedef struct VML_Call   VML_Call;

typedef enum {
    VML_OK = 0,
    VML_ERR_COMPILE,      /* syntax / type error while compiling (see vml_last_error) */
    VML_ERR_RUNTIME,      /* script raised an error, or ran out of fuel */
    VML_ERR_TYPE,         /* bad signature string, or argument type mismatch */
    VML_ERR_NOT_FOUND,    /* function / module / native binding not found */
    VML_ERR_MEMORY,
    VML_ERR_STATE,        /* call not allowed now (e.g. second load, module already finished) */
    VML_ERR_IO,
    VML_ERR_FORMAT        /* not a valid / compatible bytecode image */
} VML_Result;

/* Allocator: new_size == 0 frees. Same contract as Lua's lua_Alloc. */
typedef void *(*VML_Alloc)(void *user_data, void *ptr, size_t old_size, size_t new_size);
typedef void  (*VML_PrintFn)(void *user_data, const char *text, size_t len);
typedef VML_Result (*VML_CFunction)(VML_Call *call);

typedef struct {
    VML_Alloc alloc;              /* NULL = libc realloc/free */
    void     *alloc_ud;
    uint64_t  fuel;               /* max loop iterations per call, 0 = unlimited (sandboxing) */
    size_t    gc_threshold;       /* bytes allocated between collections, 0 = default */
    int       allow_io;           /* std.io / std.os available to scripts (default 1) */
} VML_Options;

const char *vml_version(void);
void        vml_options_default(VML_Options *opts);

VML_State  *vml_new(const VML_Options *opts);        /* NULL = defaults */
void        vml_free(VML_State *L);
const char *vml_last_error(VML_State *L);            /* message of the last failed call */

/* ---- Host modules: `import name` in scripts ---- */
VML_Module *vml_module_new(VML_State *L, const char *name);
void        vml_module_set_userdata(VML_Module *m, void *user_data);   /* see vml_call_userdata */
VML_Result  vml_module_type(VML_Module *m, const char *type_name);     /* opaque handle (int64 at runtime) */
VML_Result  vml_module_func(VML_Module *m, const char *name, const char *signature, VML_CFunction fn);
VML_Result  vml_module_const_int(VML_Module *m, const char *name, int64_t value);
VML_Result  vml_module_const_float(VML_Module *m, const char *name, double value);
VML_Result  vml_module_const_string(VML_Module *m, const char *name, const char *value);
VML_Result  vml_module_finish(VML_Module *m);

/*
 * ---- Builder API: structs, enums and functions made from C (no .vml text, no C callback) ----
 *
 *   VML_StructB *v = vml_struct_begin(m, "Vec2");
 *   vml_struct_field(v, "x", "float");
 *   vml_struct_field(v, "y", "float");
 *   vml_struct_end(v);
 *
 *   VML_FuncB *f = vml_func_begin(m, "lerp", "(a: float, b: float, t: float): float");
 *   VML_Val a = vml_b_param(f, 0), b = vml_b_param(f, 1), t = vml_b_param(f, 2);
 *   vml_b_return(f, vml_b_add(f, a, vml_b_mul(f, vml_b_sub(f, b, a), t)));   // a + (b - a) * t
 *   vml_func_end(f);
 *
 * Built functions are compiled with the module on `import`: same type checking, same bytecode,
 * same speed as `.vml`. Type errors are reported by vml_load_source with the module's name.
 * A C function (vml_module_func) may take builder structs: its C side receives the fields as
 * consecutive arguments (`(v: Vec2, k: float)` -> vml_arg_float(c, 0) = v.x, 1 = v.y, 2 = k).
 * Only one builder can be open at a time.
 */
typedef struct VML_StructB VML_StructB;
typedef struct VML_EnumB   VML_EnumB;
typedef struct VML_FuncB   VML_FuncB;
typedef int32_t            VML_Val;    /* value inside a function being built (< 0 = invalid) */

VML_StructB *vml_struct_begin(VML_Module *m, const char *name);
VML_Result   vml_struct_field(VML_StructB *s, const char *name, const char *type);
VML_Result   vml_struct_end(VML_StructB *s);

VML_EnumB   *vml_enum_begin(VML_Module *m, const char *name);
VML_Result   vml_enum_variant(VML_EnumB *e, const char *name, const char *payload);   /* payload: NULL or "(r: float)" */
VML_Result   vml_enum_end(VML_EnumB *e);

VML_FuncB   *vml_func_begin(VML_Module *m, const char *name, const char *signature);
VML_Val      vml_b_param(VML_FuncB *f, int index);
VML_Val      vml_b_int(VML_FuncB *f, int64_t value);
VML_Val      vml_b_float(VML_FuncB *f, double value);
VML_Val      vml_b_bool(VML_FuncB *f, int value);
VML_Val      vml_b_string(VML_FuncB *f, const char *value);
VML_Val      vml_b_op(VML_FuncB *f, const char *op, VML_Val a, VML_Val b);   /* + - * / % == != < <= > >= && || & | ^ << >> ?? */
VML_Val      vml_b_add(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_sub(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_mul(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_div(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_lt(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_gt(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_eq(VML_FuncB *f, VML_Val a, VML_Val b);
VML_Val      vml_b_not(VML_FuncB *f, VML_Val a);
VML_Val      vml_b_neg(VML_FuncB *f, VML_Val a);
VML_Val      vml_b_cast(VML_FuncB *f, VML_Val a, const char *type);
VML_Val      vml_b_field(VML_FuncB *f, VML_Val obj, const char *field);
VML_Val      vml_b_call(VML_FuncB *f, const char *func, const VML_Val *args, int n);   /* "clamp", "game.spawn", "Math.sqrt" */
VML_Val      vml_b_method(VML_FuncB *f, VML_Val recv, const char *method, const VML_Val *args, int n);
VML_Val      vml_b_var(VML_FuncB *f, const char *type, VML_Val init);   /* local; type NULL = inferred; init -1 = zero value */
VML_Result   vml_b_assign(VML_FuncB *f, VML_Val target, VML_Val value);  /* target: a var or a field of one */
VML_Result   vml_b_eval(VML_FuncB *f, VML_Val v);                        /* expression statement (a call) */
VML_Result   vml_b_if(VML_FuncB *f, VML_Val cond);
VML_Result   vml_b_else(VML_FuncB *f);
VML_Result   vml_b_while(VML_FuncB *f, VML_Val cond);
VML_Result   vml_b_end(VML_FuncB *f);                                    /* closes if / else / while */
VML_Result   vml_b_return(VML_FuncB *f, VML_Val v);
VML_Result   vml_b_return_void(VML_FuncB *f);
VML_Result   vml_func_end(VML_FuncB *f);

/* The module's declarations as `.vml` text (what the builder produced) — handy for debugging */
const char  *vml_module_generated_source(VML_Module *m);

/* `.vml` source compiled when a script does `import name` (text must stay valid until vml_free) */
VML_Result  vml_module_source(VML_State *L, const char *name, const char *source);

/* ---- Where `import` finds .vml packages (call before loading) ---- */
VML_Result  vml_mount_package(VML_State *L, const char *name, const char *dir);   /* `import name.x` -> dir/x.vml */
VML_Result  vml_set_lib_path(VML_State *L, const char *dir);                       /* fallback search folder */
/* Turn a module off for this state's scripts: "std.fs", or a prefix "game.debug.*". Importing it is a
   compile error that names the module. (allow_io = 0 in VML_Options turns off std.io the same way.) */
VML_Result  vml_deny_module(VML_State *L, const char *name);

/* ---- Loading & running (one program per state) ---- */
VML_Result  vml_load_source(VML_State *L, const char *chunk_name, const char *source);
VML_Result  vml_load_file(VML_State *L, const char *path);
VML_Result  vml_run_main(VML_State *L);
void        vml_set_args(VML_State *L, int argc, const char *const *argv);   /* what os.args() returns (kept by pointer) */
int         vml_exit_requested(VML_State *L, int *code);                      /* 1 if the script called os.exit */
int         vml_has_function(VML_State *L, const char *name);

/* Calls a script function. argfmt: one char per argument — 'i' int64, 'f' double, 's' const char*, 'b' int */
VML_Result  vml_call(VML_State *L, const char *func, const char *argfmt, ...);
int64_t     vml_result_int(VML_State *L);
double      vml_result_float(VML_State *L);
int         vml_result_bool(VML_State *L);
const char *vml_result_string(VML_State *L, size_t *len);   /* valid until the next call / collection */

/* ---- Inside a host function ---- */
int         vml_arg_count(VML_Call *call);
int64_t     vml_arg_int(VML_Call *call, int index);
double      vml_arg_float(VML_Call *call, int index);
int         vml_arg_bool(VML_Call *call, int index);
const char *vml_arg_string(VML_Call *call, int index, size_t *len);
VML_Result  vml_return_int(VML_Call *call, int64_t value);
VML_Result  vml_return_float(VML_Call *call, double value);
VML_Result  vml_return_bool(VML_Call *call, int value);
VML_Result  vml_return_string(VML_Call *call, const char *value, size_t len);
VML_Result  vml_error(VML_Call *call, const char *message);   /* raises a catchable script error */
void       *vml_call_userdata(VML_Call *call);
VML_State  *vml_call_state(VML_Call *call);

/* ---- Bytecode images (.vmb): run without compiling ---- */
VML_Result  vml_save_image(VML_State *L, void **out_data, size_t *out_len);   /* free with vml_free_buffer */
VML_Result  vml_save_image_file(VML_State *L, const char *path);
VML_Result  vml_load_image(VML_State *L, const void *data, size_t len);       /* register host modules first */
VML_Result  vml_load_image_file(VML_State *L, const char *path);
void        vml_free_buffer(VML_State *L, void *data);

/*
 * Interface: the program's public declarations as `.vml` text without bodies
 * (`pub func area(s: Shape): float`). Stored at the front of every image, so tools and editors
 * read types without loading code. `vml interface x.vmb` prints it.
 */
const char *vml_interface(VML_State *L, size_t *out_len);                             /* of the loaded program */
const char *vml_image_interface(const void *data, size_t len, size_t *out_len);      /* header only; points into data */

/* ---- Memory & limits ---- */
typedef struct {
    size_t   heap_bytes;          /* string bytes + container storage currently allocated */
    uint32_t collections;
    uint32_t live_arrays, live_maps, live_strings, live_closures;
} VML_MemStats;

void        vml_gc_collect(VML_State *L);                 /* between calls: collect now */
void        vml_gc_set_threshold(VML_State *L, size_t bytes);
void        vml_gc_enable(VML_State *L, int enabled);
void        vml_memory_stats(VML_State *L, VML_MemStats *out);
void        vml_set_fuel(VML_State *L, uint64_t max_loop_iterations_per_call);
void        vml_set_print(VML_State *L, VML_PrintFn fn, void *user_data);   /* NULL = stdout */

#ifdef __cplusplus
}
#endif

#endif /* VML_H */
