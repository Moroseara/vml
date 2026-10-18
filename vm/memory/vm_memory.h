#ifndef VM_MEMORY_H
#define VM_MEMORY_H

#include "../core/vm_types.h"
#include "../core/vm_isa.h"
#include "../string/vm_string.h"

/*
 * ============================================================================
 * VM KERNEL MEMORY & BYTECODE IMAGE SUBSYSTEM (`vm/memory/vm_memory.h`)
 * Self-contained fixed/arena memory pools (Zero <stdlib.h> malloc needed!)
 * ============================================================================
 */

#define VM_MAX_CODE_SIZE    65536
#define VM_MAX_CONSTANTS    1024
#define VM_INIT_STRING_POOL  256
#define VM_INIT_STRING_ARENA 4096
#define VM_INIT_INTERN_SLOTS 512
#define VM_MAX_FUNCTIONS    2048
#define VM_MAX_FILES        128
#define VM_MAX_PARAMS       16
#define VM_INIT_ARRAY_POOL  16
#define VM_INIT_ARRAY_CAP   8
#define VM_INIT_MAP_POOL    16
#define VM_MAX_UPVALUES     16

/* Function values: a plain function id, or a closure id tagged with VM_CLOSURE_TAG */
#define VM_CLOSURE_TAG      0x40000000
#define VM_IS_CLOSURE(v)    (((v) & VM_CLOSURE_TAG) != 0)

/* Where OP_CLOSURE takes each captured value from (VMFunctionInfo.upv_kind) */
#define VM_UPV_CELL         0   /* enclosing local is boxed: its register holds a cell id */
#define VM_UPV_SNAPSHOT     1   /* enclosing local is not boxed: copy its current value into a new cell */
#define VM_UPV_PARENT       2   /* enclosing function's own upvalue #index */
#define VM_INIT_MAP_CAP     8
#define VM_MAX_TASKS        64

struct VMChunkStruct;

/*
 * Host allocator hook (Lua `lua_Alloc` style). new_size == 0 means free.
 * Embedders can route all VM heap traffic through their own allocator.
 */
typedef void *(*VMReallocFn)(void *user_data, void *ptr, vm_size old_size, vm_size new_size);

typedef VMReg (*VMNativeFn)(struct VMChunkStruct *chunk, const VMReg *args, vm_u8 argc);

typedef struct {
    VMReg     *items;
    VMDataType elem_type;
    vm_u32     length;
    vm_u32     capacity;
    vm_bool    dead;       /* slot freed by the GC (on the free list) */
} VMArray;

typedef struct {
    vm_u32    *keys;
    VMReg     *vals;
    VMDataType key_type;
    VMDataType val_type;
    vm_u32     length;
    vm_u32     capacity;
    vm_bool    dead;
} VMMap;

typedef struct {
    vm_i32  func_id;
    VMReg   arg;
    VMReg   result;
    vm_u32  error_str_id;
    vm_i32  delay_ms;
    vm_u8   status; /* 0 = pending/deferred, 1 = running, 2 = completed, 3 = cancelled, 4 = errored */
} VMTaskEntry;

typedef struct {
    char       name[64];
    vm_u32     start_ip;
    vm_u8      arity;
    vm_u8      min_arity;
    vm_u8      num_regs;
    vm_bool    is_pub;
    vm_bool    is_deprecated;
    vm_bool    coerce_args_to_string;
    char       deprecated_msg[64];
    vm_i16     owner_struct_id;
    VMDataType param_types[VM_MAX_PARAMS];
    vm_i16     param_struct_ids[VM_MAX_PARAMS];
    VMReg      param_defaults[VM_MAX_PARAMS];
    vm_bool    param_has_default[VM_MAX_PARAMS];
    VMDataType return_type;
    VMDataType return_elem_types[8];
    vm_i16     return_struct_id;
    vm_i16     return_func_type_id;
    vm_u8      return_reg_count;
    vm_i32     env_map_id;
    vm_i32     enclosing_func_id;
    VMReg      upvalues[8];
    VMNativeFn native_fn;
    vm_u8      num_upvals;
    vm_u8      upv_kind[VM_MAX_UPVALUES];
    vm_u8      upv_index[VM_MAX_UPVALUES];
    vm_u8      upv_type[VM_MAX_UPVALUES];    /* VMDataType of each captured value (for the GC) */
    vm_i16     param_func_type[VM_MAX_PARAMS]; /* func-type contract id of callback params (-1 = none) */
    void      *host_fn;        /* embedder C function behind a host-module native (vml.h) */
    void      *host_ud;        /* its module's user data */
} VMFunctionInfo;

#define VM_MAX_HOST_MODULES  16
#define VM_MAX_HOST_TYPES    32
#define VM_MAX_SRC_MODULES   16

typedef struct {
    char       name[32];
    VMDataType type;           /* opaque host handles are i64 */
} VMHostType;

typedef struct {
    char        name[32];
    const char *source;        /* `.vml` text compiled on `import name` (owned by the embedder) */
} VMSourceModule;

typedef struct {
    vm_u32 func_id;
    vm_u32 first_cell;   /* index into VMChunk.closure_cells */
    vm_u8  num_cells;
    vm_bool dead;
} VMClosure;

/* Free slot ids of a GC-managed pool */
typedef struct {
    vm_u32 *ids;
    vm_u32  count, cap;
} VMFreeList;

#define VM_GC_MAX_CPUS      16
#define VM_STR_DEAD         0x01u   /* slot freed by the GC */
#define VM_STR_INTERNED     0x02u   /* entered in the intern table (compile-time & vm_chunk_add_string) */
#define VM_GC_MIN_THRESHOLD (16u * 1024u * 1024u)

typedef struct {
    char       name[64];
    VMDataType type;
    VMReg      val;
} VMModuleConst;

typedef struct {
    char       name[64];
    char       module_alias[64];
    vm_u8      num_fields;
    char       field_names[4][32];
    VMDataType field_types[4];
    VMReg      field_defaults[4];
    vm_bool    field_has_default[4];
} VMModuleStruct;

typedef struct VMChunkStruct {
    VMInst         code[VM_MAX_CODE_SIZE];
    vm_u16         code_lines[VM_MAX_CODE_SIZE];
    vm_u8          code_files[VM_MAX_CODE_SIZE];
    vm_u32         code_count;

    char           file_names[VM_MAX_FILES][256];   /* full paths, for error messages */
    vm_u8          num_files;
    vm_u8          current_file_id;
    vm_u16         current_line;

    VMReg          constants[VM_MAX_CONSTANTS];
    VMDataType     constant_types[VM_MAX_CONSTANTS];
    vm_u32         num_constants;

    VMReallocFn    realloc_fn;
    void          *alloc_ud;
    vm_bool        out_of_memory;

    VMString      *strings;
    vm_u32        *string_hashes;
    vm_u32         num_strings;
    vm_u32         strings_cap;
    char          *string_arena;
    vm_u32         string_arena_used;
    vm_u32         string_arena_cap;

    /* Open-addressing intern table: slot holds (sid + 1), 0 = empty. Entries whose sid
     * was rolled back by a frame return are stale and get skipped / reused. */
    vm_u32        *intern_slots;
    vm_u32         intern_cap;
    vm_u32         intern_used;

    VMArray       *arrays;
    vm_u32         num_arrays;
    vm_u32         arrays_cap;

    /* Closures: boxed captured variables (cells) and closure objects */
    VMReg         *cells;
    vm_u8         *cell_types;        /* VMDataType of each cell's value (VOID = unknown) */
    vm_u32         num_cells, cells_cap;
    VMClosure     *closures;
    vm_u32         num_closures, closures_cap;
    vm_u32        *closure_cells;
    vm_u32         num_closure_cells, closure_cells_cap;

    /* Garbage collector (vm/gc/vm_gc.c) */
    vm_u8         *string_dead;       /* parallel to strings[]: VM_STR_DEAD / VM_STR_INTERNED flags */
    VMFreeList     free_strings, free_arrays, free_maps, free_cells, free_closures;
    vm_u32         sealed_strings;    /* ids below this (compile-time strings) are permanent */
    vm_u32         sealed_arena;      /* arena bytes below this belong to permanent strings */
    vm_bool        gc_sealed;
    vm_bool        gc_pending;        /* set by allocators, collected at the next VM safepoint */
    vm_bool        gc_disabled;
    vm_u64         gc_allocated;      /* bytes allocated since the last collection */
    vm_u64         gc_threshold;
    vm_u32         gc_collections;
    void          *gc_cpus[VM_GC_MAX_CPUS];   /* VMCPU* currently executing on this chunk (roots) */
    vm_u32         gc_num_cpus;

    /* Embedding (vm/api/vml.c) */
    void          *host_state;                /* owning VML_State */
    vm_i32         calling_native_fid;        /* native being executed (lets one trampoline serve many) */
    vm_bool        native_error;              /* a native requested a runtime error */
    char           native_error_msg[160];
    /* Program arguments (`os.args()`) and `os.exit(code)`: exit stops the program; catch cannot stop it */
    const char *const *prog_args;
    vm_u32         prog_argc;
    vm_bool        exit_requested;
    vm_i32         exit_code;
    vm_bool        require_entry;             /* compile fails without `func main()` (CLI); off for embedding */
    char           host_modules[VM_MAX_HOST_MODULES][32];
    vm_u32         num_host_modules;
    VMHostType     host_types[VM_MAX_HOST_TYPES];
    vm_u32         num_host_types;
    VMSourceModule src_modules[VM_MAX_SRC_MODULES];
    vm_u32         num_src_modules;

    VMMap         *maps;
    vm_u32         num_maps;
    vm_u32         maps_cap;

    VMTaskEntry    tasks[VM_MAX_TASKS];
    vm_u32         num_tasks;

    vm_u32         last_error_str_id;
    vm_u32         last_traceback_str_id;
    vm_i32         active_env_map_id;
    vm_i32         active_func_id;

    VMFunctionInfo functions[VM_MAX_FUNCTIONS];
    vm_u32         num_functions;
    vm_i32         entry_func_id;

    VMModuleConst  mod_consts[64];
    vm_u32         num_mod_consts;
    char          *interface_src;    /* public declarations of the program (`.vml` text, no bodies) */
    vm_u32         interface_len;
    VMModuleStruct mod_structs[16];
    vm_u32         num_mod_structs;

    /* Native Primitive Metatable / Struct Bindings (e.g., `VM_TYPE_STRING` -> `"String"`) */
    char           primitive_struct_name[VM_TYPE_COUNT][32];
} VMChunk;

void   vm_chunk_init(VMChunk *chunk);
void   vm_chunk_init_with_alloc(VMChunk *chunk, VMReallocFn fn, void *user_data);
void   vm_chunk_free(VMChunk *chunk);
void  *vm_chunk_realloc(VMChunk *chunk, void *ptr, vm_size old_size, vm_size new_size);
/* Ensures `extra` free bytes in the string arena and returns the write cursor.
 * May move the arena: re-read any VMString copies after calling it. */
char  *vm_chunk_arena_reserve(VMChunk *chunk, vm_u32 extra);
vm_u8  vm_chunk_set_file(VMChunk *chunk, const char *file_path);
vm_u32 vm_chunk_emit(VMChunk *chunk, VMInst inst);
vm_u32 vm_chunk_add_constant(VMChunk *chunk, VMReg val, VMDataType type);
vm_u32 vm_chunk_add_string(VMChunk *chunk, const char *chars, vm_u32 length);
/* New runtime string with its own copy of the bytes (not interned, GC-managed) */
vm_u32 vm_chunk_new_string(VMChunk *chunk, const char *chars, vm_u32 length);
void   vm_chunk_set_interface(VMChunk *chunk, const char *text, vm_u32 len);
vm_u32 vm_chunk_commit_string(VMChunk *chunk, const char *dst, vm_u32 len);  /* dst from vm_chunk_arena_reserve */
vm_u32 vm_chunk_concat_strings(VMChunk *chunk, vm_u32 sid_a, vm_u32 sid_b);
vm_u32 vm_chunk_upper(VMChunk *chunk, vm_u32 sid);
vm_u32 vm_chunk_lower(VMChunk *chunk, vm_u32 sid);
vm_u32 vm_chunk_substr(VMChunk *chunk, vm_u32 sid, vm_i32 start, vm_i32 count);
vm_i32 vm_chunk_str_find(const VMChunk *chunk, vm_u32 sid, vm_u32 sub_sid);
vm_u32 vm_chunk_str_trim(VMChunk *chunk, vm_u32 sid);
vm_u32 vm_chunk_str_replace(VMChunk *chunk, vm_u32 sid, vm_u32 old_sid, vm_u32 new_sid);
vm_u32 vm_chunk_str_format(VMChunk *chunk, vm_u32 fmt_sid, vm_u32 arg_sid);
vm_i32 vm_chunk_str_cmp(const VMChunk *chunk, vm_u32 sid_a, vm_u32 sid_b);

/* Equality with cached-hash / length fast reject (strings are not all interned) */
static inline vm_bool vm_chunk_str_eq(const VMChunk *chunk, vm_u32 a, vm_u32 b) {
    if (a == b) return VM_TRUE;
    if (chunk->strings[a].length != chunk->strings[b].length ||
        chunk->string_hashes[a] != chunk->string_hashes[b]) return VM_FALSE;
    return vm_bool_from_int(vm_chunk_str_cmp(chunk, a, b) == 0);
}
VMReg  vm_math_eval(VMReg src, vm_u8 kind);
VMReg  vm_chunk_cast_value(VMChunk *chunk, VMReg src, vm_u8 type_pair);

/* GC accounting: allocators report bytes; the VM collects at its next safepoint */
static inline void vm_chunk_gc_account(VMChunk *chunk, vm_u64 bytes) {
    chunk->gc_allocated += bytes;
    if (chunk->gc_allocated >= chunk->gc_threshold && chunk->gc_disabled == VM_FALSE) chunk->gc_pending = VM_TRUE;
}
void   vm_chunk_intern_rebuild(VMChunk *chunk);

/* Closures & cells (see VM_CLOSURE_TAG) */
vm_u32  vm_chunk_cell_new(VMChunk *chunk, VMReg val, VMDataType type);
vm_i32  vm_chunk_closure_new(VMChunk *chunk, vm_u32 func_id, vm_u8 num_cells);
/* Splits a function value into its function id and closure id (-1 for plain functions) */
vm_bool vm_chunk_decode_func(const VMChunk *chunk, vm_i32 value, vm_u32 *out_func_id, vm_i32 *out_closure);

/* VM Native Array & Map Pool Operations */
vm_u32  vm_chunk_array_new(VMChunk *chunk, VMDataType elem_type);
VMReg   vm_chunk_array_get(const VMChunk *chunk, vm_u32 arr_id, vm_i32 idx);
void    vm_chunk_array_set(VMChunk *chunk, vm_u32 arr_id, vm_i32 idx, VMReg val);
void    vm_chunk_array_push(VMChunk *chunk, vm_u32 arr_id, VMReg val);
VMReg   vm_chunk_array_pop(VMChunk *chunk, vm_u32 arr_id);

vm_u32  vm_chunk_map_new(VMChunk *chunk, VMDataType key_type, VMDataType val_type);
VMReg   vm_chunk_map_get(const VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid);
void    vm_chunk_map_set(VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid, VMReg val);
vm_bool vm_chunk_map_has(const VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid);
vm_bool vm_chunk_map_del(VMChunk *chunk, vm_u32 map_id, vm_u32 key_sid);
vm_u32  vm_chunk_map_key_at(const VMChunk *chunk, vm_u32 map_id, vm_i32 idx);
VMReg   vm_chunk_map_val_at(const VMChunk *chunk, vm_u32 map_id, vm_i32 idx);

/* Luau-Style Native VM Library & Primitive Builder C API */
vm_u32 vm_lib_begin_func(VMChunk *chunk, const char *qname, vm_u8 arity, vm_u8 min_arity,
                         const VMDataType *param_types, VMDataType ret_type);
void   vm_lib_end_func(VMChunk *chunk, vm_u32 fid, vm_u8 num_regs);
vm_u32 vm_lib_add_native(VMChunk *chunk, const char *qname, vm_u8 arity,
                         const VMDataType *param_types, VMDataType ret_type, VMNativeFn native_fn);
void   vm_lib_set_coerce_string_args(VMChunk *chunk, vm_u32 fid);
void   vm_lib_add_const(VMChunk *chunk, const char *qname, VMDataType type, VMReg val);
vm_u32 vm_lib_add_struct(VMChunk *chunk, const char *struct_name, const char *module_alias);
void   vm_lib_add_struct_field(VMChunk *chunk, vm_u32 msid, const char *fname,
                               VMDataType ftype, vm_bool has_def, VMReg def_val);
void   vm_lib_bind_primitive(VMChunk *chunk, VMDataType prim_type, const char *struct_name);

#endif /* VM_MEMORY_H */
