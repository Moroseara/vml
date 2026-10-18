#include "../stdlib/vm_intrinsics.h"
#include "vml_internal.h"
#include "../stdlib/vm_stdlib.h"
#include "../string/vm_string.h"

#include <stdio.h>

/*
 * Bytecode image (.vmb), little-endian:
 *
 *   "VMLB" u32 format_version u32 api_major
 *   interface: u32 len, bytes                      (public declarations as .vml text; first, so
 *                                                   tools read types without loading code)
 *   code:      u32 count, u32 inst[count], u16 line[count], u8 file[count]
 *   files:     u32 count, {u8 len, bytes}
 *   constants: u32 count, {u8 type, u64 raw}
 *   strings:   u32 count, {u32 len, bytes}         (ids are preserved)
 *   functions: u32 count, {function record}        (natives stored by name, re-bound on load)
 *   i32 entry_func_id
 */

#define VMB_MAGIC   0x424C4D56u   /* "VMLB" */
#define VMB_VERSION 2u

#define FN_PUB      0x01u
#define FN_COERCE   0x02u
#define FN_NATIVE   0x04u
#define FN_HOST     0x08u

/* ---------------------------------------------------------------------------
 * Writer
 * ------------------------------------------------------------------------- */

typedef struct {
    VML_State *L;
    vm_u8     *data;
    size_t     len, cap;
    vm_bool    oom;
} Writer;

static void w_bytes(Writer *w, const void *src, size_t n) {
    if (w->oom) return;
    if (w->len + n > w->cap) {
        size_t nc = (w->cap > 0) ? w->cap * 2 : 4096;
        while (nc < w->len + n) nc *= 2;
        vm_u8 *nd = (vm_u8 *)vml_mem(w->L, w->data, w->cap, nc);
        if (nd == VM_NULL) {
            w->oom = VM_TRUE;
            return;
        }
        w->data = nd;
        w->cap = nc;
    }
    const vm_u8 *s = (const vm_u8 *)src;
    for (size_t i = 0; i < n; i++) w->data[w->len + i] = s[i];
    w->len += n;
}

static void w_u8(Writer *w, vm_u8 v)   { w_bytes(w, &v, 1); }
static void w_u16(Writer *w, vm_u16 v) { vm_u8 b[2] = {(vm_u8)v, (vm_u8)(v >> 8)}; w_bytes(w, b, 2); }
static void w_u32(Writer *w, vm_u32 v) {
    vm_u8 b[4] = {(vm_u8)v, (vm_u8)(v >> 8), (vm_u8)(v >> 16), (vm_u8)(v >> 24)};
    w_bytes(w, b, 4);
}
static void w_u64(Writer *w, vm_u64 v) { w_u32(w, (vm_u32)v); w_u32(w, (vm_u32)(v >> 32)); }
static void w_str8(Writer *w, const char *s) {
    vm_u32 n = vm_str_len(s);
    if (n > 255) n = 255;
    w_u8(w, (vm_u8)n);
    w_bytes(w, s, n);
}

VML_Result vml_save_image(VML_State *L, void **out_data, size_t *out_len) {
    VMChunk *c = L->chunk;
    if (!L->loaded) {
        vml_set_error(L, "nothing to save: load a program first");
        return VML_ERR_STATE;
    }
    Writer w = {L, VM_NULL, 0, 0, VM_FALSE};
    w_u32(&w, VMB_MAGIC);
    w_u32(&w, VMB_VERSION);
    w_u32(&w, VML_VERSION_MAJOR);
    w_u32(&w, c->interface_len);
    w_bytes(&w, c->interface_src, c->interface_len);

    w_u32(&w, c->code_count);
    for (vm_u32 i = 0; i < c->code_count; i++) w_u32(&w, c->code[i]);
    for (vm_u32 i = 0; i < c->code_count; i++) w_u16(&w, c->code_lines[i]);
    for (vm_u32 i = 0; i < c->code_count; i++) w_u8(&w, c->code_files[i]);

    w_u32(&w, c->num_files);
    for (vm_u32 i = 0; i < c->num_files; i++) w_str8(&w, c->file_names[i]);

    w_u32(&w, c->num_constants);
    for (vm_u32 i = 0; i < c->num_constants; i++) {
        w_u8(&w, (vm_u8)c->constant_types[i]);
        w_u64(&w, c->constants[i].raw);
    }

    w_u32(&w, c->num_strings);
    for (vm_u32 i = 0; i < c->num_strings; i++) {
        vm_u32 n = (c->string_dead[i] & VM_STR_DEAD) ? 0 : c->strings[i].length;
        w_u32(&w, n);
        w_bytes(&w, c->strings[i].chars, n);
    }

    w_u32(&w, c->num_functions);
    for (vm_u32 f = 0; f < c->num_functions; f++) {
        const VMFunctionInfo *fn = &c->functions[f];
        vm_u8 flags = 0;
        if (fn->is_pub) flags |= FN_PUB;
        if (fn->coerce_args_to_string) flags |= FN_COERCE;
        if (fn->native_fn != VM_NULL) flags |= FN_NATIVE;
        if (fn->host_fn != VM_NULL) flags |= FN_HOST;
        w_str8(&w, fn->name);
        w_u8(&w, flags);
        w_u32(&w, fn->start_ip);
        w_u8(&w, fn->arity);
        w_u8(&w, fn->min_arity);
        w_u8(&w, fn->num_regs);
        w_u16(&w, (vm_u16)fn->owner_struct_id);
        for (vm_u8 p = 0; p < fn->arity && p < VM_MAX_PARAMS; p++) {
            w_u8(&w, (vm_u8)fn->param_types[p]);
            w_u16(&w, (vm_u16)fn->param_struct_ids[p]);
            w_u8(&w, fn->param_has_default[p]);
            w_u64(&w, fn->param_defaults[p].raw);
            w_u16(&w, (vm_u16)fn->param_func_type[p]);
        }
        w_u8(&w, (vm_u8)fn->return_type);
        for (int r = 0; r < 8; r++) w_u8(&w, (vm_u8)fn->return_elem_types[r]);
        w_u16(&w, (vm_u16)fn->return_struct_id);
        w_u16(&w, (vm_u16)fn->return_func_type_id);
        w_u8(&w, fn->return_reg_count);
        w_u8(&w, fn->num_upvals);
        for (vm_u8 u = 0; u < fn->num_upvals; u++) {
            w_u8(&w, fn->upv_kind[u]);
            w_u8(&w, fn->upv_index[u]);
            w_u8(&w, fn->upv_type[u]);
        }
    }
    w_u32(&w, (vm_u32)c->entry_func_id);

    if (w.oom) {
        vml_mem(L, w.data, w.cap, 0);
        return VML_ERR_MEMORY;
    }
    /* shrink to exact size so vml_free_buffer can free with the right length */
    vm_u8 *exact = (vm_u8 *)vml_mem(L, VM_NULL, 0, w.len + 8);
    if (exact == VM_NULL) {
        vml_mem(L, w.data, w.cap, 0);
        return VML_ERR_MEMORY;
    }
    for (size_t i = 0; i < w.len; i++) exact[i + 8] = w.data[i];
    vm_u64 len64 = w.len;
    for (int i = 0; i < 8; i++) exact[i] = (vm_u8)(len64 >> (8 * i));   /* hidden size prefix */
    vml_mem(L, w.data, w.cap, 0);
    *out_data = exact + 8;
    *out_len = w.len;
    return VML_OK;
}

void vml_free_buffer(VML_State *L, void *data) {
    if (data == VM_NULL) return;
    vm_u8 *base = (vm_u8 *)data - 8;
    vm_u64 len = 0;
    for (int i = 0; i < 8; i++) len |= (vm_u64)base[i] << (8 * i);
    vml_mem(L, base, (size_t)len + 8, 0);
}

VML_Result vml_save_image_file(VML_State *L, const char *path) {
    void *data = VM_NULL;
    size_t len = 0;
    VML_Result r = vml_save_image(L, &data, &len);
    if (r != VML_OK) return r;
    FILE *f = fopen(path, "wb");
    if (f == VM_NULL) {
        vml_free_buffer(L, data);
        vml_set_error(L, "could not write image file");
        return VML_ERR_IO;
    }
    size_t put = fwrite(data, 1, len, f);
    fclose(f);
    vml_free_buffer(L, data);
    return (put == len) ? VML_OK : VML_ERR_IO;
}

/* ---------------------------------------------------------------------------
 * Reader
 * ------------------------------------------------------------------------- */

typedef struct {
    const vm_u8 *p, *end;
    vm_bool      bad;
} Reader;

static vm_bool r_need(Reader *r, size_t n) {
    if (r->bad || (size_t)(r->end - r->p) < n) {
        r->bad = VM_TRUE;
        return VM_FALSE;
    }
    return VM_TRUE;
}
static vm_u8 r_u8(Reader *r)   { if (!r_need(r, 1)) return 0; return *r->p++; }
static vm_u16 r_u16(Reader *r) { if (!r_need(r, 2)) return 0; vm_u16 v = (vm_u16)(r->p[0] | (r->p[1] << 8)); r->p += 2; return v; }
static vm_u32 r_u32(Reader *r) {
    if (!r_need(r, 4)) return 0;
    vm_u32 v = (vm_u32)r->p[0] | ((vm_u32)r->p[1] << 8) | ((vm_u32)r->p[2] << 16) | ((vm_u32)r->p[3] << 24);
    r->p += 4;
    return v;
}
static vm_u64 r_u64(Reader *r) { vm_u64 lo = r_u32(r); vm_u64 hi = r_u32(r); return lo | (hi << 32); }
static void r_str8(Reader *r, char *out, vm_u32 cap) {
    vm_u32 n = r_u8(r);
    if (!r_need(r, n)) { out[0] = '\0'; return; }
    vm_u32 k = (n < cap - 1) ? n : cap - 1;
    for (vm_u32 i = 0; i < k; i++) out[i] = (char)r->p[i];
    out[k] = '\0';
    r->p += n;
}

/* Native functions of the standard library, by name (registered into a scratch chunk) */
static VMNativeFn find_std_native(VMChunk *scratch, const char *name, vm_u8 arity) {
    vm_u32 n = vm_str_len(name);
    for (vm_u32 i = 0; i < scratch->num_functions; i++) {
        const VMFunctionInfo *fn = &scratch->functions[i];
        if (fn->native_fn != VM_NULL && fn->arity == arity && vm_str_eq(fn->name, vm_str_len(fn->name), name, n) == VM_TRUE) {
            return fn->native_fn;
        }
    }
    return VM_NULL;
}

static VMChunk *std_scratch(VML_State *L) {
    VMChunk *s = (VMChunk *)vml_mem(L, VM_NULL, 0, sizeof(VMChunk));
    if (s == VM_NULL) return VM_NULL;
    vm_chunk_init_with_alloc(s, (VMReallocFn)L->alloc, L->alloc_ud);
    vm_stdlib_register_prelude(s);
    vm_stdlib_register_math(s);
    vm_stdlib_register_string(s);
    vm_stdlib_register_array(s);
    vm_stdlib_register_map(s);
    vm_stdlib_register_bit(s);
    vm_stdlib_register_io(s);
    vm_stdlib_register_task(s);
    vm_stdlib_register_debug(s);
    vm_stdlib_register_time(s);
    vm_stdlib_register_env(s);
    vm_stdlib_register_os(s);
    vm_intrinsics_register_natives(s);
    return s;
}

static VML_Result fail(VML_State *L, VML_Result r, const char *msg) {
    vml_set_error(L, msg);
    return r;
}

/* "<prefix> '<name>'" */
static void name_msg(char *out, vm_u32 cap, const char *prefix, const char *name) {
    vm_u32 k = 0;
    for (vm_u32 i = 0; prefix[i] && k < cap - 4; i++) out[k++] = prefix[i];
    out[k++] = ' ';
    out[k++] = '\'';
    for (vm_u32 i = 0; name[i] && k < cap - 2; i++) out[k++] = name[i];
    out[k++] = '\'';
    out[k] = '\0';
}

VML_Result vml_load_image(VML_State *L, const void *data, size_t len) {
    VMChunk *c = L->chunk;
    if (L->loaded) return fail(L, VML_ERR_STATE, "a program is already loaded in this state");
    Reader r = {(const vm_u8 *)data, (const vm_u8 *)data + len, VM_FALSE};
    if (r_u32(&r) != VMB_MAGIC) return fail(L, VML_ERR_FORMAT, "not a vml bytecode image");
    if (r_u32(&r) != VMB_VERSION) return fail(L, VML_ERR_FORMAT, "bytecode image format version is not supported");
    if (r_u32(&r) != VML_VERSION_MAJOR) return fail(L, VML_ERR_FORMAT, "bytecode image was built for another major API version");
    vm_u32 iface_len = r_u32(&r);
    if (r.bad || iface_len > (vm_u32)(r.end - r.p)) return fail(L, VML_ERR_FORMAT, "image interface section is truncated");
    vm_chunk_set_interface(c, (const char *)r.p, iface_len);
    r.p += iface_len;

    vm_u32 code_count = r_u32(&r);
    if (code_count > VM_MAX_CODE_SIZE) return fail(L, VML_ERR_FORMAT, "image code section too large");
    for (vm_u32 i = 0; i < code_count; i++) c->code[i] = r_u32(&r);
    for (vm_u32 i = 0; i < code_count; i++) c->code_lines[i] = r_u16(&r);
    for (vm_u32 i = 0; i < code_count; i++) c->code_files[i] = r_u8(&r);
    c->code_count = code_count;

    vm_u32 nfiles = r_u32(&r);
    if (nfiles > VM_MAX_FILES) return fail(L, VML_ERR_FORMAT, "image has too many source files");
    for (vm_u32 i = 0; i < nfiles; i++) r_str8(&r, c->file_names[i], 256);
    c->num_files = (vm_u8)nfiles;
    c->current_file_id = 0;

    vm_u32 nconst = r_u32(&r);
    if (nconst > VM_MAX_CONSTANTS) return fail(L, VML_ERR_FORMAT, "image has too many constants");
    for (vm_u32 i = 0; i < nconst; i++) {
        c->constant_types[i] = (VMDataType)r_u8(&r);
        c->constants[i].raw = r_u64(&r);
    }
    c->num_constants = nconst;

    /* Strings keep their ids: start from an empty pool */
    vm_u32 nstr = r_u32(&r);
    c->num_strings = 0;
    c->string_arena_used = 0;
    c->free_strings.count = 0;
    for (vm_u32 i = 0; i < nstr && !r.bad; i++) {
        vm_u32 n = r_u32(&r);
        if (!r_need(&r, n)) break;
        vm_u32 sid = vm_chunk_new_string(c, (const char *)r.p, n);
        if (sid != i) return fail(L, VML_ERR_MEMORY, "out of memory loading image strings");
        c->string_dead[sid] = VM_STR_INTERNED;
        r.p += n;
    }
    vm_chunk_intern_rebuild(c);
    c->last_error_str_id = 0;
    c->last_traceback_str_id = 0;

    vm_u32 nfuncs = r_u32(&r);
    if (nfuncs > VM_MAX_FUNCTIONS) return fail(L, VML_ERR_FORMAT, "image has too many functions");
    VMChunk *scratch = VM_NULL;
    VML_Result result = VML_OK;
    for (vm_u32 f = 0; f < nfuncs && !r.bad; f++) {
        VMFunctionInfo *fn = &c->functions[f];
        r_str8(&r, fn->name, 64);
        vm_u8 flags = r_u8(&r);
        fn->start_ip = r_u32(&r);
        fn->arity = r_u8(&r);
        fn->min_arity = r_u8(&r);
        fn->num_regs = r_u8(&r);
        fn->owner_struct_id = (vm_i16)r_u16(&r);
        if (fn->arity > VM_MAX_PARAMS) { r.bad = VM_TRUE; break; }
        for (vm_u8 p = 0; p < fn->arity; p++) {
            fn->param_types[p] = (VMDataType)r_u8(&r);
            fn->param_struct_ids[p] = (vm_i16)r_u16(&r);
            fn->param_has_default[p] = r_u8(&r);
            fn->param_defaults[p].raw = r_u64(&r);
            fn->param_func_type[p] = (vm_i16)r_u16(&r);
        }
        fn->return_type = (VMDataType)r_u8(&r);
        for (int k = 0; k < 8; k++) fn->return_elem_types[k] = (VMDataType)r_u8(&r);
        fn->return_struct_id = (vm_i16)r_u16(&r);
        fn->return_func_type_id = (vm_i16)r_u16(&r);
        fn->return_reg_count = r_u8(&r);
        fn->num_upvals = r_u8(&r);
        if (fn->num_upvals > VM_MAX_UPVALUES) { r.bad = VM_TRUE; break; }
        for (vm_u8 u = 0; u < fn->num_upvals; u++) {
            fn->upv_kind[u] = r_u8(&r);
            fn->upv_index[u] = r_u8(&r);
            fn->upv_type[u] = r_u8(&r);
        }
        fn->is_pub = vm_bool_from_int((flags & FN_PUB) != 0);
        fn->coerce_args_to_string = vm_bool_from_int((flags & FN_COERCE) != 0);
        fn->is_deprecated = VM_FALSE;
        fn->deprecated_msg[0] = '\0';
        fn->env_map_id = -1;
        fn->enclosing_func_id = -1;
        fn->native_fn = VM_NULL;
        fn->host_fn = VM_NULL;
        fn->host_ud = VM_NULL;

        if (flags & FN_HOST) {
            /* `<module>.<func>` must be registered (and finished) on this state */
            vm_u32 nlen = vm_str_len(fn->name), dot = 0;
            while (dot < nlen && fn->name[dot] != '.') dot++;
            VML_Module *m = vml_find_module(L, fn->name, dot);
            const VML_FuncRec *fr = VM_NULL;
            if (m != VM_NULL && dot < nlen) {
                for (vm_u32 k = 0; k < m->num_funcs; k++) {
                    if (vm_str_eq_cstr(fn->name + dot + 1, nlen - dot - 1, m->funcs[k].name) &&
                        m->funcs[k].num_params == fn->arity) fr = &m->funcs[k];
                }
            }
            if (fr == VM_NULL) {
                char msg[160];
                name_msg(msg, 160, "image needs host function (register its module first):", fn->name);
                result = fail(L, VML_ERR_NOT_FOUND, msg);
                break;
            }
            fn->native_fn = vml_host_trampoline;
            fn->host_fn = (void *)fr->fn;
            fn->host_ud = m->user_data;
        } else if (flags & FN_NATIVE) {
            if (scratch == VM_NULL) scratch = std_scratch(L);
            if (scratch == VM_NULL) {
                result = fail(L, VML_ERR_MEMORY, "out of memory binding natives");
                break;
            }
            fn->native_fn = find_std_native(scratch, fn->name, fn->arity);
            if (fn->native_fn == VM_NULL) {
                char msg[160];
                name_msg(msg, 160, "image needs a native function this VM does not have:", fn->name);
                result = fail(L, VML_ERR_NOT_FOUND, msg);
                break;
            }
        }
    }
    c->num_functions = nfuncs;
    c->entry_func_id = (vm_i32)r_u32(&r);
    if (scratch != VM_NULL) {
        vm_chunk_free(scratch);
        vml_mem(L, scratch, sizeof(VMChunk), 0);
    }
    if (result != VML_OK) return result;
    if (r.bad) return fail(L, VML_ERR_FORMAT, "bytecode image is truncated or corrupt");
    if (c->entry_func_id >= (vm_i32)nfuncs) return fail(L, VML_ERR_FORMAT, "bytecode image has an invalid entry point");
    L->loaded = VM_TRUE;
    return VML_OK;
}

VML_Result vml_load_image_file(VML_State *L, const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == VM_NULL) return fail(L, VML_ERR_IO, "could not open image file");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return fail(L, VML_ERR_FORMAT, "empty image file");
    }
    vm_u8 *buf = (vm_u8 *)vml_mem(L, VM_NULL, 0, (size_t)size);
    if (buf == VM_NULL) {
        fclose(f);
        return VML_ERR_MEMORY;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    VML_Result r = vml_load_image(L, buf, got);
    vml_mem(L, buf, (size_t)size, 0);
    return r;
}

/* Reads only the header: the interface text inside an image (pointer into `data`, not copied) */
const char *vml_image_interface(const void *data, size_t len, size_t *out_len) {
    const vm_u8 *p = (const vm_u8 *)data;
    if (out_len) *out_len = 0;
    if (data == VM_NULL || len < 16) return VM_NULL;
    vm_u32 h[4];
    for (int i = 0; i < 4; i++) h[i] = (vm_u32)p[i * 4] | ((vm_u32)p[i * 4 + 1] << 8) | ((vm_u32)p[i * 4 + 2] << 16) | ((vm_u32)p[i * 4 + 3] << 24);
    if (h[0] != VMB_MAGIC || h[1] != VMB_VERSION || (size_t)h[3] > len - 16) return VM_NULL;
    if (out_len) *out_len = h[3];
    return (const char *)(p + 16);
}

const char *vml_interface(VML_State *L, size_t *out_len) {
    if (out_len) *out_len = L->chunk->interface_len;
    return L->chunk->interface_src ? L->chunk->interface_src : "";
}
