#include "vml_internal.h"
#include "../gc/vm_gc.h"
#include "../syscall/vm_syscall.h"
#include "../string/vm_string.h"
#include "../../compiler/compiler.h"

#include <stdarg.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Basics
 * ------------------------------------------------------------------------- */

const char *vml_version(void) {
    return "0.1.0";
}

void vml_options_default(VML_Options *opts) {
    opts->alloc = VM_NULL;
    opts->alloc_ud = VM_NULL;
    opts->fuel = 0;
    opts->gc_threshold = 0;
    opts->allow_io = 1;
}

void *vml_mem(VML_State *L, void *ptr, size_t old_size, size_t new_size) {
    return L->alloc(L->alloc_ud, ptr, old_size, new_size);
}

static void *default_alloc(void *ud, void *ptr, size_t old_size, size_t new_size) {
    return vm_sys_default_realloc(ud, ptr, (vm_size)old_size, (vm_size)new_size);
}

void vml_set_error(VML_State *L, const char *msg) {
    vm_u32 n = vm_str_len(msg);
    if (n >= sizeof(L->error)) n = sizeof(L->error) - 1;
    for (vm_u32 i = 0; i < n; i++) L->error[i] = msg[i];
    L->error[n] = '\0';
}

const char *vml_last_error(VML_State *L) {
    return L->error;
}

VML_State *vml_new(const VML_Options *opts) {
    VML_Options def;
    if (opts == VM_NULL) {
        vml_options_default(&def);
        opts = &def;
    }
    VML_Alloc alloc = (opts->alloc != VM_NULL) ? opts->alloc : default_alloc;
    VML_State *L = (VML_State *)alloc(opts->alloc_ud, VM_NULL, 0, sizeof(VML_State));
    if (L == VM_NULL) return VM_NULL;
    L->alloc = alloc;
    L->alloc_ud = opts->alloc_ud;
    L->chunk = (VMChunk *)alloc(opts->alloc_ud, VM_NULL, 0, sizeof(VMChunk));
    L->cpu = (VMCPU *)alloc(opts->alloc_ud, VM_NULL, 0, sizeof(VMCPU));
    if (L->chunk == VM_NULL || L->cpu == VM_NULL) {
        if (L->chunk) alloc(opts->alloc_ud, L->chunk, sizeof(VMChunk), 0);
        if (L->cpu) alloc(opts->alloc_ud, L->cpu, sizeof(VMCPU), 0);
        alloc(opts->alloc_ud, L, sizeof(VML_State), 0);
        return VM_NULL;
    }
    vm_chunk_init_with_alloc(L->chunk, (VMReallocFn)alloc, opts->alloc_ud);
    L->chunk->host_state = L;
    L->chunk->require_entry = VM_FALSE;
    if (opts->gc_threshold > 0) L->chunk->gc_threshold = opts->gc_threshold;
    vm_cpu_init(L->cpu);
    vm_config_init_default(&L->cfg);
    if (!opts->allow_io) vm_config_disable(&L->cfg, VM_FEAT_STD_IO);
    L->num_modules = 0;
    L->loaded = VM_FALSE;
    L->source_copy = VM_NULL;
    L->source_len = 0;
    L->fuel = opts->fuel;
    L->error[0] = '\0';
    L->result.raw = 0;
    L->result_type = VM_TYPE_VOID;
    L->print_fn = VM_NULL;
    L->print_ud = VM_NULL;
    return L;
}

static VML_State *g_print_state = VM_NULL;

void vml_free(VML_State *L) {
    if (L == VM_NULL) return;
    if (g_print_state == L) {
        g_print_state = VM_NULL;
        vm_sys_set_output_callback(VM_NULL);
    }
    for (vm_u32 i = 0; i < L->num_modules; i++) {
        VML_Module *m = L->modules[i];
        for (vm_u32 k = 0; k < m->num_consts; k++) {
            if (m->consts[k].str) vml_mem(L, m->consts[k].str, vm_str_len(m->consts[k].str) + 1, 0);
        }
        if (m->src) vml_mem(L, m->src, m->src_cap, 0);
        vml_mem(L, m, sizeof(VML_Module), 0);
    }
    vm_chunk_free(L->chunk);
    if (L->source_copy) vml_mem(L, L->source_copy, L->source_len + 1, 0);
    vml_mem(L, L->chunk, sizeof(VMChunk), 0);
    vml_mem(L, L->cpu, sizeof(VMCPU), 0);
    L->alloc(L->alloc_ud, L, sizeof(VML_State), 0);
}

/* ---------------------------------------------------------------------------
 * Signatures: "(name: string, hp: int): Entity"
 * ------------------------------------------------------------------------- */

static vm_bool is_word_char(char ch) {
    return vm_bool_from_int((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_');
}

static void skip_ws(const char **s) {
    while (**s == ' ' || **s == '\t') (*s)++;
}

/* Parses one type word; *out_struct = index of a builder struct (its first field's type is returned), else -1 */
vm_bool vml_parse_type_word(VML_Module *m, const char **s, VMDataType *out, int *out_struct) {
    if (out_struct != VM_NULL) *out_struct = -1;
    skip_ws(s);
    const char *w = *s;
    while (is_word_char(**s)) (*s)++;
    vm_u32 n = (vm_u32)(*s - w);
    if (n == 0) return VM_FALSE;
    VMDataType t;
    if (vm_str_eq_cstr(w, n, "i32")) t = VM_TYPE_I32;
    else if (vm_str_eq_cstr(w, n, "int") || vm_str_eq_cstr(w, n, "i64")) t = VM_TYPE_I64;
    else if (vm_str_eq_cstr(w, n, "float") || vm_str_eq_cstr(w, n, "f32")) t = VM_TYPE_F32;
    else if (vm_str_eq_cstr(w, n, "f64") || vm_str_eq_cstr(w, n, "double")) t = VM_TYPE_F64;
    else if (vm_str_eq_cstr(w, n, "bool")) t = VM_TYPE_BOOL;
    else if (vm_str_eq_cstr(w, n, "string")) t = VM_TYPE_STRING;
    else if (vm_str_eq_cstr(w, n, "void")) t = VM_TYPE_VOID;
    else if (vm_str_eq_cstr(w, n, "array")) t = VM_TYPE_ARRAY;
    else if (vm_str_eq_cstr(w, n, "map")) t = VM_TYPE_MAP;
    else {
        vm_bool found = VM_FALSE;
        for (vm_u32 i = 0; i < m->num_types; i++) {
            if (vm_str_eq_cstr(w, n, m->types[i])) found = VM_TRUE;
        }
        if (found) {
            t = VM_TYPE_I64;   /* opaque host handle */
        } else {
            VML_StructRec *sr = vml_find_struct(m, w, n);
            if (sr == VM_NULL || out_struct == VM_NULL) return VM_FALSE;
            *out_struct = (int)(sr - m->structs);
            t = sr->field_types[0];
        }
    }
    skip_ws(s);
    while ((*s)[0] == '[' && (*s)[1] == ']') {
        *s += 2;
        t = VM_TYPE_ARRAY;
    }
    *out = t;
    return VM_TRUE;
}

static vm_bool parse_signature(VML_Module *m, const char *sig, VML_FuncRec *f) {
    const char *s = sig;
    skip_ws(&s);
    if (*s++ != '(') return VM_FALSE;
    f->num_params = 0;
    skip_ws(&s);
    if (*s != ')') {
        for (;;) {
            /* optional `name:` */
            const char *save = s;
            skip_ws(&s);
            while (is_word_char(*s)) s++;
            skip_ws(&s);
            if (*s == ':') s++;
            else s = save;
            if (f->num_params >= VM_MAX_PARAMS) return VM_FALSE;
            if (!vml_parse_type_word(m, &s, &f->params[f->num_params++], VM_NULL)) return VM_FALSE;
            skip_ws(&s);
            if (*s == ',') { s++; continue; }
            break;
        }
    }
    skip_ws(&s);
    if (*s++ != ')') return VM_FALSE;
    skip_ws(&s);
    f->ret = VM_TYPE_VOID;
    if (*s == ':') {
        s++;
        if (!vml_parse_type_word(m, &s, &f->ret, VM_NULL)) return VM_FALSE;
    }
    skip_ws(&s);
    return vm_bool_from_int(*s == '\0');
}

/* ---------------------------------------------------------------------------
 * Host modules
 * ------------------------------------------------------------------------- */

VML_Module *vml_find_module(VML_State *L, const char *name, vm_u32 len) {
    for (vm_u32 i = 0; i < L->num_modules; i++) {
        if (vm_str_eq_cstr(name, len, L->modules[i]->name)) return L->modules[i];
    }
    return VM_NULL;
}

VML_Module *vml_module_new(VML_State *L, const char *name) {
    if (L->loaded || L->num_modules >= VML_MAX_MODULES || vm_str_len(name) >= 32) {
        vml_set_error(L, "vml_module_new: too many modules, name too long, or program already loaded");
        return VM_NULL;
    }
    if (vml_find_module(L, name, vm_str_len(name)) != VM_NULL) {
        vml_set_error(L, "vml_module_new: module already exists");
        return VM_NULL;
    }
    VML_Module *m = (VML_Module *)vml_mem(L, VM_NULL, 0, sizeof(VML_Module));
    if (m == VM_NULL) return VM_NULL;
    m->L = L;
    vm_str_copy(m->name, name, vm_str_len(name), 32);
    m->user_data = VM_NULL;
    m->num_funcs = m->num_consts = m->num_types = m->num_structs = 0;
    m->src = VM_NULL;
    m->src_len = m->src_cap = 0;
    m->building = VM_FALSE;
    m->finished = VM_FALSE;
    L->modules[L->num_modules++] = m;
    return m;
}

void vml_module_set_userdata(VML_Module *m, void *user_data) {
    m->user_data = user_data;
}

VML_Result vml_module_type(VML_Module *m, const char *type_name) {
    if (m->finished || m->num_types >= VML_MAX_MOD_TYPES || vm_str_len(type_name) >= 32) return VML_ERR_STATE;
    vm_str_copy(m->types[m->num_types++], type_name, vm_str_len(type_name), 32);
    return VML_OK;
}

/*
 * A C function whose signature uses builder structs: registered as a raw native taking the
 * flattened fields, plus a generated typed wrapper `name(v: Vec2) -> __raw_name(v.x, v.y)`.
 */
static VML_Result module_func_structs(VML_Module *m, const char *name, const char *signature, VML_CFunction fn) {
    char raw_sig[256];
    char wrap[512];
    char call[256];
    vm_u32 rs = 0, ws = 0, cs = 0;
    #define PUT(buf, pos, cap, str) do { const char *_p = (str); while (*_p && pos + 1 < cap) buf[pos++] = *_p++; buf[pos] = '\0'; } while (0)
    PUT(raw_sig, rs, sizeof(raw_sig), "(");
    PUT(wrap, ws, sizeof(wrap), "pub func ");
    PUT(wrap, ws, sizeof(wrap), name);
    PUT(wrap, ws, sizeof(wrap), signature);
    PUT(call, cs, sizeof(call), "__raw_");
    PUT(call, cs, sizeof(call), name);
    PUT(call, cs, sizeof(call), "(");
    const char *s = signature;
    skip_ws(&s);
    if (*s++ != '(') return VML_ERR_TYPE;
    int pi = 0;
    vm_bool first = VM_TRUE;
    skip_ws(&s);
    while (*s != ')' && *s != '\0') {
        const char *pn = s;
        while (is_word_char(*s)) s++;
        vm_u32 pnl = (vm_u32)(s - pn);
        skip_ws(&s);
        if (*s != ':' || pnl == 0) {
            vml_set_error(m->L, "vml_module_func: parameters need names when a struct type is used, e.g. \"(v: Vec2): f32\"");
            return VML_ERR_TYPE;
        }
        s++;
        const char *tw = s;
        VMDataType t;
        int sidx = -1;
        if (!vml_parse_type_word(m, &s, &t, &sidx)) return VML_ERR_TYPE;
        char pname[40];
        vm_str_copy(pname, pn, pnl, 40);
        if (sidx >= 0) {
            VML_StructRec *sr = &m->structs[sidx];
            for (vm_u8 f = 0; f < sr->num_fields; f++) {
                char piece[96];
                vm_u32 k = 0;
                if (!first) { PUT(raw_sig, rs, sizeof(raw_sig), ", "); PUT(call, cs, sizeof(call), ", "); }
                first = VM_FALSE;
                piece[0] = '\0';
                PUT(piece, k, sizeof(piece), "p");
                char num[8];
                vm_u32 nl = vm_i64_to_str(num, pi++);
                num[nl] = '\0';
                PUT(piece, k, sizeof(piece), num);
                PUT(piece, k, sizeof(piece), ": ");
                PUT(piece, k, sizeof(piece), vm_type_name(sr->field_types[f]));
                PUT(raw_sig, rs, sizeof(raw_sig), piece);
                PUT(call, cs, sizeof(call), pname);
                PUT(call, cs, sizeof(call), ".");
                PUT(call, cs, sizeof(call), sr->field_names[f]);
            }
        } else {
            char piece[96];
            vm_u32 k = 0;
            if (!first) { PUT(raw_sig, rs, sizeof(raw_sig), ", "); PUT(call, cs, sizeof(call), ", "); }
            first = VM_FALSE;
            piece[0] = '\0';
            PUT(piece, k, sizeof(piece), "p");
            char num[8];
            vm_u32 nl = vm_i64_to_str(num, pi++);
            num[nl] = '\0';
            PUT(piece, k, sizeof(piece), num);
            PUT(piece, k, sizeof(piece), ": ");
            char tword[40];
            vm_str_copy(tword, tw, (vm_u32)(s - tw), 40);
            PUT(piece, k, sizeof(piece), tword);
            PUT(raw_sig, rs, sizeof(raw_sig), piece);
            PUT(call, cs, sizeof(call), pname);
        }
        skip_ws(&s);
        if (*s == ',') { s++; skip_ws(&s); }
    }
    if (*s != ')') return VML_ERR_TYPE;
    s++;
    PUT(raw_sig, rs, sizeof(raw_sig), ")");
    PUT(call, cs, sizeof(call), ")");
    skip_ws(&s);
    vm_bool has_ret = VM_FALSE;
    if (*s == ':') {
        const char *rt = s + 1;
        VMDataType t;
        int sidx = -1;
        const char *probe = rt;
        if (!vml_parse_type_word(m, &probe, &t, &sidx)) return VML_ERR_TYPE;
        if (sidx >= 0) {
            vml_set_error(m->L, "vml_module_func: C functions cannot return a struct yet (return its fields, or build it in a builder function)");
            return VML_ERR_TYPE;
        }
        has_ret = (t != VM_TYPE_VOID);
        PUT(raw_sig, rs, sizeof(raw_sig), ": ");
        PUT(raw_sig, rs, sizeof(raw_sig), rt);
    }
    char raw_name[40];
    vm_u32 rn = 0;
    raw_name[0] = '\0';
    PUT(raw_name, rn, sizeof(raw_name), "__raw_");
    PUT(raw_name, rn, sizeof(raw_name), name);
    VML_Result r = vml_module_func(m, raw_name, raw_sig, fn);
    if (r != VML_OK) return r;
    PUT(wrap, ws, sizeof(wrap), " { ");
    if (has_ret) PUT(wrap, ws, sizeof(wrap), "return ");
    PUT(wrap, ws, sizeof(wrap), call);
    PUT(wrap, ws, sizeof(wrap), " }\n");
    #undef PUT
    return vml_src_append_cstr(m, wrap) ? VML_OK : VML_ERR_MEMORY;
}

static vm_bool signature_uses_structs(VML_Module *m, const char *sig) {
    for (vm_u32 i = 0; i < m->num_structs; i++) {
        const char *nm = m->structs[i].name;
        vm_u32 nl = vm_str_len(nm);
        for (const char *p = sig; *p; p++) {
            if ((p == sig || !is_word_char(p[-1])) && vm_str_eq(p, nl, nm, nl) && !is_word_char(p[nl])) return VM_TRUE;
        }
    }
    return VM_FALSE;
}

VML_Result vml_module_func(VML_Module *m, const char *name, const char *signature, VML_CFunction fn) {
    if (m->finished || m->num_funcs >= VML_MAX_MOD_FUNCS || vm_str_len(name) >= 32 || fn == VM_NULL) return VML_ERR_STATE;
    if (signature_uses_structs(m, signature)) return module_func_structs(m, name, signature, fn);
    VML_FuncRec *f = &m->funcs[m->num_funcs];
    vm_str_copy(f->name, name, vm_str_len(name), 32);
    f->fn = fn;
    if (!parse_signature(m, signature, f)) {
        vml_set_error(m->L, "vml_module_func: invalid signature (use e.g. \"(name: string, hp: int): int\")");
        return VML_ERR_TYPE;
    }
    m->num_funcs++;
    return VML_OK;
}

static VML_Result add_const(VML_Module *m, const char *name, VMDataType type, VMReg val, const char *str) {
    if (m->finished || m->num_consts >= VML_MAX_MOD_CONSTS || vm_str_len(name) >= 32) return VML_ERR_STATE;
    VML_ConstRec *k = &m->consts[m->num_consts];
    vm_str_copy(k->name, name, vm_str_len(name), 32);
    k->type = type;
    k->val = val;
    k->str = VM_NULL;
    if (str != VM_NULL) {
        vm_u32 n = vm_str_len(str);
        k->str = (char *)vml_mem(m->L, VM_NULL, 0, n + 1);
        if (k->str == VM_NULL) return VML_ERR_MEMORY;
        for (vm_u32 i = 0; i <= n; i++) k->str[i] = str[i];
    }
    m->num_consts++;
    return VML_OK;
}

VML_Result vml_module_const_int(VML_Module *m, const char *name, int64_t value) {
    VMReg v = {0};
    if (value >= -2147483647LL - 1 && value <= 2147483647LL) {
        v.i32 = (vm_i32)value;
        return add_const(m, name, VM_TYPE_I32, v, VM_NULL);
    }
    v.i64 = value;
    return add_const(m, name, VM_TYPE_I64, v, VM_NULL);
}

VML_Result vml_module_const_float(VML_Module *m, const char *name, double value) {
    VMReg v = {0};
    v.f32 = (vm_f32)value;
    return add_const(m, name, VM_TYPE_F32, v, VM_NULL);
}

VML_Result vml_module_const_string(VML_Module *m, const char *name, const char *value) {
    VMReg v = {0};
    return add_const(m, name, VM_TYPE_STRING, v, value);
}

VML_Result vml_module_finish(VML_Module *m) {
    if (m->finished) return VML_ERR_STATE;
    if (m->building) {
        vml_set_error(m->L, "vml_module_finish: a struct / enum / function builder is still open");
        return VML_ERR_STATE;
    }
    m->finished = VM_TRUE;
    return VML_OK;
}

VML_Result vml_module_source(VML_State *L, const char *name, const char *source) {
    VMChunk *c = L->chunk;
    if (L->loaded || c->num_src_modules >= VM_MAX_SRC_MODULES || vm_str_len(name) >= 32) return VML_ERR_STATE;
    VMSourceModule *sm = &c->src_modules[c->num_src_modules++];
    vm_str_copy(sm->name, name, vm_str_len(name), 32);
    sm->source = source;
    return VML_OK;
}

/* One native trampoline serves every host function: the VM records which one is running */
VMReg vml_host_trampoline(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    VMFunctionInfo *fn = &chunk->functions[chunk->calling_native_fid];
    VML_Call call;
    call.L = (VML_State *)chunk->host_state;
    call.chunk = chunk;
    call.args = args;
    call.argc = argc;
    call.fn = fn;
    call.user_data = fn->host_ud;
    call.ret.raw = 0;
    VML_Result r = ((VML_CFunction)fn->host_fn)(&call);
    if (r != VML_OK && chunk->native_error == VM_FALSE) {
        chunk->native_error = VM_TRUE;
        vm_str_copy(chunk->native_error_msg, "host function failed", 20, sizeof(chunk->native_error_msg));
    }
    return call.ret;
}

/* Registers finished host modules into the chunk (as natives `<mod>.<name>`) before compiling */
static VML_Result install_modules(VML_State *L) {
    VMChunk *c = L->chunk;
    for (vm_u32 i = 0; i < L->num_modules; i++) {
        VML_Module *m = L->modules[i];
        if (!m->finished) {
            vml_set_error(L, "host module was not finished (call vml_module_finish)");
            return VML_ERR_STATE;
        }
        vm_str_copy(c->host_modules[c->num_host_modules++], m->name, vm_str_len(m->name), 32);
        /* Builder declarations (structs, enums, built functions, typed wrappers) compile on `import` */
        if (m->src_len > 0) {
            if (c->num_src_modules >= VM_MAX_SRC_MODULES) {
                vml_set_error(L, "too many source modules");
                return VML_ERR_STATE;
            }
            VMSourceModule *sm = &c->src_modules[c->num_src_modules++];
            vm_str_copy(sm->name, m->name, vm_str_len(m->name), 32);
            sm->source = m->src;
        }
        for (vm_u32 t = 0; t < m->num_types && c->num_host_types < VM_MAX_HOST_TYPES; t++) {
            VMHostType *ht = &c->host_types[c->num_host_types++];
            vm_str_copy(ht->name, m->types[t], vm_str_len(m->types[t]), 32);
            ht->type = VM_TYPE_I64;
        }
        for (vm_u32 f = 0; f < m->num_funcs; f++) {
            VML_FuncRec *fr = &m->funcs[f];
            char qname[64];
            vm_str_join_dot(qname, 64, m->name, vm_str_len(m->name), fr->name, vm_str_len(fr->name));
            vm_u32 fid = vm_lib_add_native(c, qname, fr->num_params, fr->params, fr->ret, vml_host_trampoline);
            c->functions[fid].host_fn = (void *)fr->fn;
            c->functions[fid].host_ud = m->user_data;
        }
        for (vm_u32 k = 0; k < m->num_consts; k++) {
            VML_ConstRec *kr = &m->consts[k];
            char qname[64];
            vm_str_join_dot(qname, 64, m->name, vm_str_len(m->name), kr->name, vm_str_len(kr->name));
            VMReg v = kr->val;
            if (kr->type == VM_TYPE_STRING) v.str_id = vm_chunk_new_string(c, kr->str, vm_str_len(kr->str));
            vm_lib_add_const(c, qname, kr->type, v);
        }
    }
    return VML_OK;
}

/* ---------------------------------------------------------------------------
 * Loading
 * ------------------------------------------------------------------------- */

VML_Result vml_load_source(VML_State *L, const char *chunk_name, const char *source) {
    if (L->loaded) {
        vml_set_error(L, "a program is already loaded in this state");
        return VML_ERR_STATE;
    }
    VML_Result r = install_modules(L);
    if (r != VML_OK) return r;

    /* Keep our own copy: some compiled data may point into the source text */
    vm_u32 n = vm_str_len(source);
    L->source_copy = (char *)vml_mem(L, VM_NULL, 0, n + 1);
    if (L->source_copy == VM_NULL) return VML_ERR_MEMORY;
    for (vm_u32 i = 0; i <= n; i++) L->source_copy[i] = source[i];
    L->source_len = n;

    (void)vm_chunk_set_file(L->chunk, (chunk_name != VM_NULL) ? chunk_name : "<script>");
    vm_sys_clear_output_buffer();
    vm_sys_enable_output_buffer(VM_TRUE);
    vm_sys_set_output_silent(VM_TRUE);
    vm_bool ok = vm_compile_source_with_config(L->source_copy, L->chunk, &L->cfg);
    vm_sys_set_output_silent(VM_FALSE);
    vm_sys_enable_output_buffer(VM_FALSE);
    if (!ok) {
        vm_u32 len = 0;
        const char *msg = vm_sys_get_output_buffer(&len);
        while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == ' ')) len--;
        char buf[512];
        vm_u32 k = (len < sizeof(buf) - 1) ? len : (vm_u32)sizeof(buf) - 1;
        for (vm_u32 i = 0; i < k; i++) buf[i] = msg[i];
        buf[k] = '\0';
        vml_set_error(L, (k > 0) ? buf : "compile error");
        vm_sys_clear_output_buffer();
        return VML_ERR_COMPILE;
    }
    vm_sys_clear_output_buffer();
    L->loaded = VM_TRUE;
    return VML_OK;
}

static VML_Result read_file(VML_State *L, const char *path, char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == VM_NULL) {
        vml_set_error(L, "could not open file");
        return VML_ERR_IO;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return VML_ERR_IO;
    }
    char *buf = (char *)vml_mem(L, VM_NULL, 0, (size_t)size + 1);
    if (buf == VM_NULL) {
        fclose(f);
        return VML_ERR_MEMORY;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    *out_len = got;
    return VML_OK;
}

VML_Result vml_mount_package(VML_State *L, const char *name, const char *dir) {
    if (L == VM_NULL || name == VM_NULL || dir == VM_NULL) return VML_ERR_STATE;
    if (L->cfg.num_pkg_mounts >= VM_MAX_PKG_MOUNTS) {
        vml_set_error(L, "vml_mount_package: too many mounted packages");
        return VML_ERR_STATE;
    }
    vm_config_mount_package(&L->cfg, name, dir);
    return VML_OK;
}

VML_Result vml_set_lib_path(VML_State *L, const char *dir) {
    if (L == VM_NULL || dir == VM_NULL) return VML_ERR_STATE;
    vm_config_set_lib_path(&L->cfg, dir);
    return VML_OK;
}

VML_Result vml_load_file(VML_State *L, const char *path) {
    char *src = VM_NULL;
    size_t len = 0;
    VML_Result r = read_file(L, path, &src, &len);
    if (r != VML_OK) return r;
    r = vml_load_source(L, path, src);
    vml_mem(L, src, len + 1, 0);
    return r;
}

/* ---------------------------------------------------------------------------
 * Running
 * ------------------------------------------------------------------------- */

static VML_Result run_function(VML_State *L, vm_i32 fid, const VMReg *args, vm_u8 argc) {
    VMChunk *c = L->chunk;
    VMCPU *cpu = L->cpu;
    vm_cpu_init(cpu);
    cpu->catch_top_level = VM_TRUE;
    cpu->max_instructions = L->fuel;
    for (vm_u8 i = 0; i < argc; i++) cpu->registers[i] = args[i];
    vm_i32 saved_entry = c->entry_func_id;
    c->entry_func_id = fid;
    VMReg r = vm_cpu_execute(cpu, c);
    c->entry_func_id = saved_entry;
    if (c->exit_requested) return VML_OK;   /* os.exit: not an error; see vml_exit_requested */
    if (cpu->has_error) {
        if (L->fuel > 0 && cpu->executed_loops > L->fuel) {
            vml_set_error(L, "execution fuel limit exceeded");
        } else {
            vm_u32 sid = c->last_traceback_str_id;
            if (sid >= c->num_strings || c->strings[sid].length == 0) sid = cpu->last_error_str_id;
            char buf[512];
            vm_u32 n = (sid < c->num_strings) ? c->strings[sid].length : 0;
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            for (vm_u32 i = 0; i < n; i++) buf[i] = c->strings[sid].chars[i];
            buf[n] = '\0';
            vml_set_error(L, (n > 0) ? buf : "runtime error");
        }
        c->last_traceback_str_id = c->last_error_str_id;
        return VML_ERR_RUNTIME;
    }
    L->result = r;
    L->result_type = c->functions[fid].return_type;
    return VML_OK;
}

VML_Result vml_run_main(VML_State *L) {
    if (!L->loaded) return VML_ERR_STATE;
    if (L->chunk->entry_func_id < 0) {
        vml_set_error(L, "program has no 'func main()'");
        return VML_ERR_NOT_FOUND;
    }
    return run_function(L, L->chunk->entry_func_id, VM_NULL, 0);
}

void vml_set_args(VML_State *L, int argc, const char *const *argv) {
    if (L == VM_NULL) return;
    L->chunk->prog_args = argv;
    L->chunk->prog_argc = (argc > 0) ? (vm_u32)argc : 0;
}

int vml_exit_requested(VML_State *L, int *code) {
    if (L == VM_NULL || !L->chunk->exit_requested) return 0;
    if (code != VM_NULL) *code = L->chunk->exit_code;
    return 1;
}

static vm_i32 find_script_func(VML_State *L, const char *name) {
    VMChunk *c = L->chunk;
    vm_u32 n = vm_str_len(name);
    for (vm_u32 i = 0; i < c->num_functions; i++) {
        VMFunctionInfo *fn = &c->functions[i];
        if (fn->native_fn == VM_NULL && fn->start_ip != 0xFFFFFFFFu &&
            vm_str_eq(fn->name, vm_str_len(fn->name), name, n) == VM_TRUE) return (vm_i32)i;
    }
    return -1;
}

int vml_has_function(VML_State *L, const char *name) {
    return L->loaded && find_script_func(L, name) >= 0;
}

static VMReg reg_from_int(VMDataType t, int64_t v) {
    VMReg r = {0};
    switch (t) {
        case VM_TYPE_I64:  r.i64 = v; break;
        case VM_TYPE_F32:  r.f32 = (vm_f32)v; break;
        case VM_TYPE_F64:  r.f64 = (vm_f64)v; break;
        case VM_TYPE_BOOL: r.b = vm_bool_from_int(v != 0); break;
        default:           r.i32 = (vm_i32)v; break;
    }
    return r;
}

static VMReg reg_from_float(VMDataType t, double v) {
    VMReg r = {0};
    switch (t) {
        case VM_TYPE_F32: r.f32 = (vm_f32)v; break;
        case VM_TYPE_F64: r.f64 = v; break;
        case VM_TYPE_I64: r.i64 = (vm_i64)v; break;
        default:          r.i32 = (vm_i32)v; break;
    }
    return r;
}

static int64_t int_from_reg(VMDataType t, VMReg r) {
    switch (t) {
        case VM_TYPE_I64:  return r.i64;
        case VM_TYPE_F32:  return (int64_t)r.f32;
        case VM_TYPE_F64:  return (int64_t)r.f64;
        case VM_TYPE_BOOL: return r.b ? 1 : 0;
        default:           return r.i32;
    }
}

static double float_from_reg(VMDataType t, VMReg r) {
    switch (t) {
        case VM_TYPE_F32: return r.f32;
        case VM_TYPE_F64: return r.f64;
        case VM_TYPE_I64: return (double)r.i64;
        default:          return (double)r.i32;
    }
}

VML_Result vml_call(VML_State *L, const char *func, const char *argfmt, ...) {
    if (!L->loaded) return VML_ERR_STATE;
    vm_i32 fid = find_script_func(L, func);
    if (fid < 0) {
        vml_set_error(L, "no script function with that name");
        return VML_ERR_NOT_FOUND;
    }
    VMFunctionInfo *fn = &L->chunk->functions[fid];
    vm_u32 argc = (argfmt != VM_NULL) ? vm_str_len(argfmt) : 0;
    if (argc != fn->arity) {
        vml_set_error(L, "argument count does not match the script function");
        return VML_ERR_TYPE;
    }
    VMReg args[VM_MAX_PARAMS];
    va_list ap;
    va_start(ap, argfmt);
    for (vm_u32 i = 0; i < argc; i++) {
        VMDataType pt = fn->param_types[i];
        if (fn->param_struct_ids[i] >= 0) {
            va_end(ap);
            vml_set_error(L, "struct parameters cannot be passed through vml_call");
            return VML_ERR_TYPE;
        }
        switch (argfmt[i]) {
            case 'i': args[i] = reg_from_int(pt, va_arg(ap, int64_t)); break;
            case 'b': args[i] = reg_from_int(pt, va_arg(ap, int)); break;
            case 'f': args[i] = reg_from_float(pt, va_arg(ap, double)); break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (pt != VM_TYPE_STRING) {
                    va_end(ap);
                    vml_set_error(L, "string passed for a non-string parameter");
                    return VML_ERR_TYPE;
                }
                args[i].raw = 0;
                args[i].str_id = vm_chunk_new_string(L->chunk, s, vm_str_len(s));
                break;
            }
            default:
                va_end(ap);
                vml_set_error(L, "unknown argument format character (use i, f, s, b)");
                return VML_ERR_TYPE;
        }
    }
    va_end(ap);
    return run_function(L, fid, args, (vm_u8)argc);
}

int64_t vml_result_int(VML_State *L)    { return int_from_reg(L->result_type, L->result); }
double  vml_result_float(VML_State *L)  { return float_from_reg(L->result_type, L->result); }
int     vml_result_bool(VML_State *L)   { return int_from_reg(L->result_type, L->result) != 0; }

const char *vml_result_string(VML_State *L, size_t *len) {
    VMChunk *c = L->chunk;
    if (L->result_type != VM_TYPE_STRING || L->result.str_id >= c->num_strings) {
        if (len) *len = 0;
        return "";
    }
    if (len) *len = c->strings[L->result.str_id].length;
    return c->strings[L->result.str_id].chars;
}

/* ---------------------------------------------------------------------------
 * Host function helpers
 * ------------------------------------------------------------------------- */

int vml_arg_count(VML_Call *call) {
    return call->argc;
}

static vm_bool arg_ok(VML_Call *call, int i) {
    return vm_bool_from_int(i >= 0 && i < call->argc && i < VM_MAX_PARAMS);
}

int64_t vml_arg_int(VML_Call *call, int i) {
    return arg_ok(call, i) ? int_from_reg(call->fn->param_types[i], call->args[i]) : 0;
}

double vml_arg_float(VML_Call *call, int i) {
    return arg_ok(call, i) ? float_from_reg(call->fn->param_types[i], call->args[i]) : 0.0;
}

int vml_arg_bool(VML_Call *call, int i) {
    return arg_ok(call, i) ? (int_from_reg(call->fn->param_types[i], call->args[i]) != 0) : 0;
}

const char *vml_arg_string(VML_Call *call, int i, size_t *len) {
    if (!arg_ok(call, i) || call->fn->param_types[i] != VM_TYPE_STRING || call->args[i].str_id >= call->chunk->num_strings) {
        if (len) *len = 0;
        return "";
    }
    const VMString *s = &call->chunk->strings[call->args[i].str_id];
    if (len) *len = s->length;
    return s->chars;
}

VML_Result vml_return_int(VML_Call *call, int64_t v) {
    call->ret = reg_from_int(call->fn->return_type, v);
    return VML_OK;
}

VML_Result vml_return_float(VML_Call *call, double v) {
    call->ret = reg_from_float(call->fn->return_type, v);
    return VML_OK;
}

VML_Result vml_return_bool(VML_Call *call, int v) {
    call->ret.raw = 0;
    call->ret.b = vm_bool_from_int(v != 0);
    return VML_OK;
}

VML_Result vml_return_string(VML_Call *call, const char *v, size_t len) {
    if (call->fn->return_type != VM_TYPE_STRING) return vml_error(call, "host function returned a string for a non-string result");
    call->ret.raw = 0;
    call->ret.str_id = vm_chunk_new_string(call->chunk, v, (vm_u32)len);
    return VML_OK;
}

VML_Result vml_error(VML_Call *call, const char *message) {
    VMChunk *c = call->chunk;
    c->native_error = VM_TRUE;
    vm_str_copy(c->native_error_msg, message, vm_str_len(message), sizeof(c->native_error_msg));
    return VML_ERR_RUNTIME;
}

void *vml_call_userdata(VML_Call *call) { return call->user_data; }
VML_State *vml_call_state(VML_Call *call) { return call->L; }

/* ---------------------------------------------------------------------------
 * Memory, limits, output
 * ------------------------------------------------------------------------- */

void vml_gc_collect(VML_State *L) {
    VMChunk *c = L->chunk;
    if (c->gc_num_cpus > 0) {           /* called from a host function: collect at the next safepoint */
        c->gc_pending = VM_TRUE;
        return;
    }
    vm_gc_seal(c);
    vm_gc_collect(c);
}

void vml_gc_set_threshold(VML_State *L, size_t bytes) {
    L->chunk->gc_threshold = (bytes < 65536) ? 65536 : bytes;
}

void vml_gc_enable(VML_State *L, int enabled) {
    L->chunk->gc_disabled = vm_bool_from_int(!enabled);
}

void vml_memory_stats(VML_State *L, VML_MemStats *out) {
    VMChunk *c = L->chunk;
    size_t heap = c->string_arena_cap + (size_t)c->strings_cap * (sizeof(VMString) + 5);
    for (vm_u32 i = 0; i < c->num_arrays; i++) heap += (size_t)c->arrays[i].capacity * sizeof(VMReg);
    for (vm_u32 i = 0; i < c->num_maps; i++) heap += (size_t)c->maps[i].capacity * (sizeof(VMReg) + 4);
    heap += (size_t)c->cells_cap * (sizeof(VMReg) + 1) + (size_t)c->closures_cap * sizeof(VMClosure);
    out->heap_bytes = heap;
    out->collections = c->gc_collections;
    out->live_arrays = c->num_arrays - c->free_arrays.count;
    out->live_maps = c->num_maps - c->free_maps.count;
    out->live_strings = c->num_strings - c->free_strings.count;
    out->live_closures = c->num_closures - c->free_closures.count;
}

void vml_set_fuel(VML_State *L, uint64_t max_loop_iterations_per_call) {
    L->fuel = max_loop_iterations_per_call;
}

static void print_trampoline(const char *buf, vm_size len) {
    if (g_print_state != VM_NULL && g_print_state->print_fn != VM_NULL) {
        g_print_state->print_fn(g_print_state->print_ud, buf, (size_t)len);
    }
}

void vml_set_print(VML_State *L, VML_PrintFn fn, void *user_data) {
    L->print_fn = fn;
    L->print_ud = user_data;
    if (fn != VM_NULL) {
        g_print_state = L;
        vm_sys_set_output_callback(print_trampoline);
    } else if (g_print_state == L) {
        g_print_state = VM_NULL;
        vm_sys_set_output_callback(VM_NULL);
    }
}
