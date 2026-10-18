#include "vml_internal.h"
#include "../string/vm_string.h"

/*
 * Builder API: structs, enums and functions created from C without writing `.vml`.
 *
 * Every builder call produces `.vml` declarations in the module's generated source. On
 * `import <module>` that source goes through the normal compiler, so built functions get
 * the same type checking, the same bytecode and the same speed as hand-written `.vml`,
 * and the module's interface is simply that declaration text.
 */

/* ---------------------------------------------------------------------------
 * Generated source buffer
 * ------------------------------------------------------------------------- */

vm_bool vml_src_append(VML_Module *m, const char *text, vm_u32 len) {
    if (m->src_len + len + 1 > m->src_cap) {
        vm_u32 cap = (m->src_cap > 0) ? m->src_cap : 512;
        while (cap < m->src_len + len + 1) cap *= 2;
        char *ns = (char *)vml_mem(m->L, m->src, m->src_cap, cap);
        if (ns == VM_NULL) return VM_FALSE;
        m->src = ns;
        m->src_cap = cap;
    }
    for (vm_u32 i = 0; i < len; i++) m->src[m->src_len++] = text[i];
    m->src[m->src_len] = '\0';
    return VM_TRUE;
}

vm_bool vml_src_append_cstr(VML_Module *m, const char *text) {
    return vml_src_append(m, text, vm_str_len(text));
}

VML_StructRec *vml_find_struct(VML_Module *m, const char *name, vm_u32 len) {
    for (vm_u32 i = 0; i < m->num_structs; i++) {
        if (vm_str_eq_cstr(name, len, m->structs[i].name)) return &m->structs[i];
    }
    return VM_NULL;
}

static vm_bool is_ident(const char *s) {
    if (s == VM_NULL || s[0] == '\0') return VM_FALSE;
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) return VM_FALSE;
    for (const char *p = s; *p; p++) {
        char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) return VM_FALSE;
    }
    return vm_bool_from_int(vm_str_len(s) < 32);
}

/* A type written by the host: a known type word, optionally followed by `[]` or `?` */
static vm_bool valid_type_text(VML_Module *m, const char *type) {
    if (type == VM_NULL) return VM_FALSE;
    const char *s = type;
    VMDataType t;
    int sidx;
    if (!vml_parse_type_word(m, &s, &t, &sidx)) return VM_FALSE;
    while (*s == ' ') s++;
    if (*s == '?') s++;
    while (*s == ' ') s++;
    return vm_bool_from_int(*s == '\0');
}

static vm_bool begin_build(VML_Module *m, const char *what) {
    if (m == VM_NULL) return VM_FALSE;
    if (m->finished || m->building) {
        char msg[96];
        vm_u32 k = 0;
        const char *a = "builder: module already finished or another builder is open (";
        for (vm_u32 i = 0; a[i] && k < 80; i++) msg[k++] = a[i];
        for (vm_u32 i = 0; what[i] && k < 92; i++) msg[k++] = what[i];
        msg[k++] = ')';
        msg[k] = '\0';
        vml_set_error(m->L, msg);
        return VM_FALSE;
    }
    m->building = VM_TRUE;
    return VM_TRUE;
}

/* ---------------------------------------------------------------------------
 * Structs
 * ------------------------------------------------------------------------- */

struct VML_StructB {
    VML_Module    *m;
    VML_StructRec *rec;
    vm_bool        failed;
};

VML_StructB *vml_struct_begin(VML_Module *m, const char *name) {
    if (!is_ident(name)) {
        if (m) vml_set_error(m->L, "vml_struct_begin: invalid struct name");
        return VM_NULL;
    }
    if (m->num_structs >= VML_MAX_MOD_STRUCTS || vml_find_struct(m, name, vm_str_len(name)) != VM_NULL) {
        vml_set_error(m->L, "vml_struct_begin: too many structs or duplicate name");
        return VM_NULL;
    }
    if (!begin_build(m, "struct")) return VM_NULL;
    VML_StructB *b = (VML_StructB *)vml_mem(m->L, VM_NULL, 0, sizeof(VML_StructB));
    if (b == VM_NULL) {
        m->building = VM_FALSE;
        return VM_NULL;
    }
    b->m = m;
    b->rec = &m->structs[m->num_structs];
    vm_str_copy(b->rec->name, name, vm_str_len(name), 32);
    b->rec->num_fields = 0;
    b->failed = VM_FALSE;
    return b;
}

VML_Result vml_struct_field(VML_StructB *b, const char *name, const char *type) {
    if (b == VM_NULL) return VML_ERR_STATE;
    VMDataType t;
    int sidx = -1;
    const char *s = type;
    if (!is_ident(name) || b->rec->num_fields >= VML_MAX_STRUCT_FIELDS ||
        !vml_parse_type_word(b->m, &s, &t, &sidx) || sidx >= 0 || *s != '\0') {
        vml_set_error(b->m->L, "vml_struct_field: invalid field name or type (fields are numbers, bool, string, arrays, maps or host handles)");
        b->failed = VM_TRUE;
        return VML_ERR_TYPE;
    }
    VML_StructRec *r = b->rec;
    vm_str_copy(r->field_names[r->num_fields], name, vm_str_len(name), 32);
    r->field_types[r->num_fields] = t;
    r->num_fields++;
    return VML_OK;
}

VML_Result vml_struct_end(VML_StructB *b) {
    if (b == VM_NULL) return VML_ERR_STATE;
    VML_Module *m = b->m;
    VML_Result res = VML_OK;
    if (b->failed || b->rec->num_fields == 0) {
        if (!b->failed) vml_set_error(m->L, "vml_struct_end: a struct needs at least one field");
        res = VML_ERR_TYPE;
    } else {
        VML_StructRec *r = b->rec;
        vml_src_append_cstr(m, "pub struct ");
        vml_src_append_cstr(m, r->name);
        vml_src_append_cstr(m, " {\n");
        for (vm_u8 f = 0; f < r->num_fields; f++) {
            vml_src_append_cstr(m, "    pub ");
            vml_src_append_cstr(m, r->field_names[f]);
            vml_src_append_cstr(m, ": ");
            vml_src_append_cstr(m, vm_type_name(r->field_types[f]));
            vml_src_append_cstr(m, "\n");
        }
        if (!vml_src_append_cstr(m, "}\n\n")) res = VML_ERR_MEMORY;
        else m->num_structs++;
    }
    m->building = VM_FALSE;
    vml_mem(m->L, b, sizeof(VML_StructB), 0);
    return res;
}

/* ---------------------------------------------------------------------------
 * Enums
 * ------------------------------------------------------------------------- */

struct VML_EnumB {
    VML_Module *m;
    char        text[2048];
    vm_u32      len;
    vm_u32      variants;
    vm_bool     failed;
};

static void eb_put(VML_EnumB *b, const char *s) {
    for (vm_u32 i = 0; s[i] && b->len + 1 < sizeof(b->text); i++) b->text[b->len++] = s[i];
    b->text[b->len] = '\0';
}

VML_EnumB *vml_enum_begin(VML_Module *m, const char *name) {
    if (!is_ident(name)) {
        if (m) vml_set_error(m->L, "vml_enum_begin: invalid enum name");
        return VM_NULL;
    }
    if (!begin_build(m, "enum")) return VM_NULL;
    VML_EnumB *b = (VML_EnumB *)vml_mem(m->L, VM_NULL, 0, sizeof(VML_EnumB));
    if (b == VM_NULL) {
        m->building = VM_FALSE;
        return VM_NULL;
    }
    b->m = m;
    b->len = 0;
    b->variants = 0;
    b->failed = VM_FALSE;
    b->text[0] = '\0';
    eb_put(b, "pub enum ");
    eb_put(b, name);
    eb_put(b, " {\n");
    return b;
}

/* payload: NULL, or a parameter list like "(r: float)" / "(w: float, h: float)" */
VML_Result vml_enum_variant(VML_EnumB *b, const char *name, const char *payload) {
    if (b == VM_NULL) return VML_ERR_STATE;
    if (!is_ident(name)) {
        vml_set_error(b->m->L, "vml_enum_variant: invalid variant name");
        b->failed = VM_TRUE;
        return VML_ERR_TYPE;
    }
    if (payload != VM_NULL) {
        /* each `name: type` must be a known type */
        const char *s = payload;
        while (*s == ' ') s++;
        if (*s++ != '(') b->failed = VM_TRUE;
        while (!b->failed && *s && *s != ')') {
            while (*s == ' ' || *s == ',') s++;
            while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_') s++;
            while (*s == ' ') s++;
            if (*s++ != ':') { b->failed = VM_TRUE; break; }
            VMDataType t;
            int sidx = -1;
            if (!vml_parse_type_word(b->m, &s, &t, &sidx) || sidx >= 0) { b->failed = VM_TRUE; break; }
            while (*s == ' ') s++;
        }
        if (b->failed || *s != ')') {
            vml_set_error(b->m->L, "vml_enum_variant: payload must look like \"(r: float, tag: string)\"");
            b->failed = VM_TRUE;
            return VML_ERR_TYPE;
        }
    }
    eb_put(b, (b->variants > 0) ? ",\n    " : "    ");
    eb_put(b, name);
    if (payload != VM_NULL) eb_put(b, payload);
    b->variants++;
    return VML_OK;
}

VML_Result vml_enum_end(VML_EnumB *b) {
    if (b == VM_NULL) return VML_ERR_STATE;
    VML_Module *m = b->m;
    VML_Result res = VML_OK;
    if (b->failed || b->variants == 0 || b->len + 8 >= sizeof(b->text)) {
        if (!b->failed) vml_set_error(m->L, "vml_enum_end: an enum needs at least one variant");
        res = VML_ERR_TYPE;
    } else {
        eb_put(b, "\n}\n\n");
        if (!vml_src_append(m, b->text, b->len)) res = VML_ERR_MEMORY;
    }
    m->building = VM_FALSE;
    vml_mem(m->L, b, sizeof(VML_EnumB), 0);
    return res;
}

/* ---------------------------------------------------------------------------
 * Functions: an expression tree recorded as text, compiled with the module
 * ------------------------------------------------------------------------- */

#define VML_FB_MAX_VALS 512
#define VML_FB_MAX_PARAMS 16

struct VML_FuncB {
    VML_Module *m;
    char        header[320];
    char       *body;
    vm_u32      body_len, body_cap;
    char       *vals[VML_FB_MAX_VALS];
    vm_u32      val_len[VML_FB_MAX_VALS];
    vm_u32      num_vals;
    char        params[VML_FB_MAX_PARAMS][32];
    vm_u32      num_params;
    int         depth;          /* open if / while blocks */
    vm_u32      num_locals;
    vm_bool     failed;
};

static void fb_fail(VML_FuncB *f, const char *msg) {
    if (!f->failed) vml_set_error(f->m->L, msg);
    f->failed = VM_TRUE;
}

static void body_put(VML_FuncB *f, const char *s, vm_u32 n) {
    if (f->failed) return;
    if (f->body_len + n + 1 > f->body_cap) {
        vm_u32 cap = (f->body_cap > 0) ? f->body_cap : 256;
        while (cap < f->body_len + n + 1) cap *= 2;
        char *nb = (char *)vml_mem(f->m->L, f->body, f->body_cap, cap);
        if (nb == VM_NULL) {
            fb_fail(f, "builder: out of memory");
            return;
        }
        f->body = nb;
        f->body_cap = cap;
    }
    for (vm_u32 i = 0; i < n; i++) f->body[f->body_len++] = s[i];
    f->body[f->body_len] = '\0';
}

static void body_cstr(VML_FuncB *f, const char *s) {
    body_put(f, s, vm_str_len(s));
}

static void body_indent(VML_FuncB *f) {
    for (int i = 0; i <= f->depth; i++) body_cstr(f, "    ");
}

/* Stores expression text, returns its handle */
static VML_Val new_val(VML_FuncB *f, const char *const *parts, int nparts) {
    if (f == VM_NULL || f->failed) return -1;
    if (f->num_vals >= VML_FB_MAX_VALS) {
        fb_fail(f, "builder: too many values in one function");
        return -1;
    }
    vm_u32 total = 0;
    for (int i = 0; i < nparts; i++) total += vm_str_len(parts[i]);
    char *t = (char *)vml_mem(f->m->L, VM_NULL, 0, total + 1);
    if (t == VM_NULL) {
        fb_fail(f, "builder: out of memory");
        return -1;
    }
    vm_u32 k = 0;
    for (int i = 0; i < nparts; i++) {
        for (vm_u32 j = 0; parts[i][j]; j++) t[k++] = parts[i][j];
    }
    t[k] = '\0';
    f->vals[f->num_vals] = t;
    f->val_len[f->num_vals] = total + 1;
    return (VML_Val)f->num_vals++;
}

static const char *val_text(VML_FuncB *f, VML_Val v) {
    if (f == VM_NULL || v < 0 || (vm_u32)v >= f->num_vals) {
        if (f) fb_fail(f, "builder: invalid value handle (an earlier builder call failed?)");
        return VM_NULL;
    }
    return f->vals[v];
}

VML_FuncB *vml_func_begin(VML_Module *m, const char *name, const char *signature) {
    if (!is_ident(name)) {
        if (m) vml_set_error(m->L, "vml_func_begin: invalid function name");
        return VM_NULL;
    }
    if (!begin_build(m, "function")) return VM_NULL;
    VML_FuncB *f = (VML_FuncB *)vml_mem(m->L, VM_NULL, 0, sizeof(VML_FuncB));
    if (f == VM_NULL) {
        m->building = VM_FALSE;
        return VM_NULL;
    }
    f->m = m;
    f->body = VM_NULL;
    f->body_len = f->body_cap = 0;
    f->num_vals = 0;
    f->num_params = 0;
    f->depth = 0;
    f->num_locals = 0;
    f->failed = VM_FALSE;

    /* Signature "(a: float, b: float): float": collect parameter names and check every type */
    const char *s = signature;
    while (s && *s == ' ') s++;
    if (s == VM_NULL || *s != '(') {
        fb_fail(f, "vml_func_begin: signature must look like \"(a: float, b: float): float\"");
    } else {
        s++;
        while (!f->failed && *s && *s != ')') {
            while (*s == ' ' || *s == ',') s++;
            if (*s == ')') break;
            const char *pn = s;
            while ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_') s++;
            vm_u32 pl = (vm_u32)(s - pn);
            while (*s == ' ') s++;
            if (pl == 0 || pl >= 32 || *s != ':' || f->num_params >= VML_FB_MAX_PARAMS) {
                fb_fail(f, "vml_func_begin: every parameter needs a name and a type, e.g. \"(x: float)\"");
                break;
            }
            s++;
            vm_str_copy(f->params[f->num_params++], pn, pl, 32);
            VMDataType t;
            int sidx = -1;
            if (!vml_parse_type_word(m, &s, &t, &sidx)) {
                fb_fail(f, "vml_func_begin: unknown parameter type");
                break;
            }
            while (*s == ' ') s++;
        }
        if (!f->failed && *s == ')') {
            s++;
            while (*s == ' ') s++;
            if (*s == ':') {
                const char *rt = s + 1;
                while (*rt == ' ') rt++;
                if (!valid_type_text(m, rt)) fb_fail(f, "vml_func_begin: unknown return type");
            } else if (*s != '\0') {
                fb_fail(f, "vml_func_begin: unexpected text after the parameter list");
            }
        } else if (!f->failed) {
            fb_fail(f, "vml_func_begin: missing ')'");
        }
    }
    vm_u32 k = 0;
    const char *pre = "pub func ";
    for (vm_u32 i = 0; pre[i]; i++) f->header[k++] = pre[i];
    for (vm_u32 i = 0; name[i] && k < 60; i++) f->header[k++] = name[i];
    for (vm_u32 i = 0; signature && signature[i] && k + 4 < sizeof(f->header); i++) f->header[k++] = signature[i];
    f->header[k] = '\0';
    return f;
}

VML_Val vml_b_param(VML_FuncB *f, int index) {
    if (f == VM_NULL || index < 0 || (vm_u32)index >= f->num_params) {
        if (f) fb_fail(f, "vml_b_param: no such parameter");
        return -1;
    }
    const char *parts[1] = {f->params[index]};
    return new_val(f, parts, 1);
}

VML_Val vml_b_int(VML_FuncB *f, int64_t v) {
    char buf[32];
    vm_u32 n = vm_i64_to_str(buf, v);
    buf[n] = '\0';
    const char *parts[3] = {"(", buf, ")"};
    return new_val(f, parts, 3);
}

VML_Val vml_b_float(VML_FuncB *f, double v) {
    /* fixed-point text keeps the literal parseable: integer part '.' 9 decimals */
    char buf[64];
    vm_u32 k = 0;
    double a = v;
    if (a < 0) {
        buf[k++] = '-';
        a = -a;
    }
    int64_t ip = (int64_t)a;
    double frac = a - (double)ip;
    k += vm_i64_to_str(buf + k, ip);
    buf[k++] = '.';
    for (int i = 0; i < 9; i++) {
        frac *= 10.0;
        int d = (int)frac;
        buf[k++] = (char)('0' + d);
        frac -= d;
    }
    buf[k] = '\0';
    const char *parts[3] = {"(", buf, ")"};
    return new_val(f, parts, 3);
}

VML_Val vml_b_bool(VML_FuncB *f, int v) {
    const char *parts[1] = {v ? "true" : "false"};
    return new_val(f, parts, 1);
}

VML_Val vml_b_string(VML_FuncB *f, const char *s) {
    if (f == VM_NULL || s == VM_NULL) return -1;
    vm_u32 n = vm_str_len(s);
    char *esc = (char *)vml_mem(f->m->L, VM_NULL, 0, n * 2 + 3);
    if (esc == VM_NULL) {
        fb_fail(f, "builder: out of memory");
        return -1;
    }
    vm_u32 k = 0;
    esc[k++] = '"';
    for (vm_u32 i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == '"' || ch == '\\') { esc[k++] = '\\'; esc[k++] = ch; }
        else if (ch == '\n') { esc[k++] = '\\'; esc[k++] = 'n'; }
        else if (ch == '\t') { esc[k++] = '\\'; esc[k++] = 't'; }
        else esc[k++] = ch;
    }
    esc[k++] = '"';
    esc[k] = '\0';
    const char *parts[1] = {esc};
    VML_Val v = new_val(f, parts, 1);
    vml_mem(f->m->L, esc, n * 2 + 3, 0);
    return v;
}

static vm_bool allowed_op(const char *op) {
    static const char *ops[] = {"+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||",
                                "&", "|", "^", "<<", ">>", "??", VM_NULL};
    for (int i = 0; ops[i]; i++) {
        if (vm_str_eq_cstr(op, vm_str_len(op), ops[i])) return VM_TRUE;
    }
    return VM_FALSE;
}

VML_Val vml_b_op(VML_FuncB *f, const char *op, VML_Val a, VML_Val b) {
    if (f == VM_NULL) return -1;
    if (op == VM_NULL || !allowed_op(op)) {
        fb_fail(f, "vml_b_op: unknown operator");
        return -1;
    }
    const char *ta = val_text(f, a), *tb = val_text(f, b);
    if (ta == VM_NULL || tb == VM_NULL) return -1;
    const char *parts[7] = {"(", ta, " ", op, " ", tb, ")"};
    return new_val(f, parts, 7);
}

VML_Val vml_b_add(VML_FuncB *f, VML_Val a, VML_Val b) { return vml_b_op(f, "+", a, b); }
VML_Val vml_b_sub(VML_FuncB *f, VML_Val a, VML_Val b) { return vml_b_op(f, "-", a, b); }
VML_Val vml_b_mul(VML_FuncB *f, VML_Val a, VML_Val b) { return vml_b_op(f, "*", a, b); }
VML_Val vml_b_div(VML_FuncB *f, VML_Val a, VML_Val b) { return vml_b_op(f, "/", a, b); }
VML_Val vml_b_lt(VML_FuncB *f, VML_Val a, VML_Val b)  { return vml_b_op(f, "<", a, b); }
VML_Val vml_b_gt(VML_FuncB *f, VML_Val a, VML_Val b)  { return vml_b_op(f, ">", a, b); }
VML_Val vml_b_eq(VML_FuncB *f, VML_Val a, VML_Val b)  { return vml_b_op(f, "==", a, b); }

VML_Val vml_b_not(VML_FuncB *f, VML_Val a) {
    const char *ta = val_text(f, a);
    if (ta == VM_NULL) return -1;
    const char *parts[3] = {"(!", ta, ")"};
    return new_val(f, parts, 3);
}

VML_Val vml_b_neg(VML_FuncB *f, VML_Val a) {
    const char *ta = val_text(f, a);
    if (ta == VM_NULL) return -1;
    const char *parts[3] = {"(-", ta, ")"};
    return new_val(f, parts, 3);
}

VML_Val vml_b_cast(VML_FuncB *f, VML_Val a, const char *type) {
    const char *ta = val_text(f, a);
    if (ta == VM_NULL) return -1;
    if (!valid_type_text(f->m, type)) {
        fb_fail(f, "vml_b_cast: unknown type");
        return -1;
    }
    const char *parts[5] = {"(", ta, " as ", type, ")"};
    return new_val(f, parts, 5);
}

VML_Val vml_b_field(VML_FuncB *f, VML_Val obj, const char *field) {
    const char *to = val_text(f, obj);
    if (to == VM_NULL) return -1;
    if (!is_ident(field)) {
        fb_fail(f, "vml_b_field: invalid field name");
        return -1;
    }
    const char *parts[3] = {to, ".", field};
    return new_val(f, parts, 3);
}

/* Builds "callee(a, b, ...)"; callee is a function name, `mod.func`, or "recv:method" text */
static VML_Val call_text(VML_FuncB *f, const char *callee, const VML_Val *args, int n) {
    if (f == VM_NULL || f->failed) return -1;
    if (n < 0 || n > 16) {
        fb_fail(f, "vml_b_call: too many arguments");
        return -1;
    }
    const char *parts[40];
    int np = 0;
    parts[np++] = callee;
    parts[np++] = "(";
    for (int i = 0; i < n; i++) {
        const char *t = val_text(f, args[i]);
        if (t == VM_NULL) return -1;
        if (i > 0) parts[np++] = ", ";
        parts[np++] = t;
    }
    parts[np++] = ")";
    return new_val(f, parts, np);
}

static vm_bool valid_callee(const char *name) {
    if (name == VM_NULL || name[0] == '\0' || vm_str_len(name) >= 64) return VM_FALSE;
    for (const char *p = name; *p; p++) {
        char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.')) return VM_FALSE;
    }
    return VM_TRUE;
}

VML_Val vml_b_call(VML_FuncB *f, const char *func, const VML_Val *args, int n) {
    if (f == VM_NULL) return -1;
    if (!valid_callee(func)) {
        fb_fail(f, "vml_b_call: invalid function name");
        return -1;
    }
    return call_text(f, func, args, n);
}

VML_Val vml_b_method(VML_FuncB *f, VML_Val recv, const char *method, const VML_Val *args, int n) {
    const char *tr = val_text(f, recv);
    if (tr == VM_NULL) return -1;
    if (!is_ident(method)) {
        fb_fail(f, "vml_b_method: invalid method name");
        return -1;
    }
    const char *parts[3] = {tr, ":", method};
    VML_Val callee = new_val(f, parts, 3);
    if (callee < 0) return -1;
    return call_text(f, f->vals[callee], args, n);
}

/* `let name[: type] = init` (init may be -1 with a type: zero value). Returns the variable. */
VML_Val vml_b_var(VML_FuncB *f, const char *type, VML_Val init) {
    if (f == VM_NULL || f->failed) return -1;
    if (type != VM_NULL && !valid_type_text(f->m, type)) {
        fb_fail(f, "vml_b_var: unknown type");
        return -1;
    }
    if (type == VM_NULL && init < 0) {
        fb_fail(f, "vml_b_var: needs a type or an initial value");
        return -1;
    }
    char name[16];
    vm_u32 k = 0;
    name[k++] = 'v';
    name[k++] = '_';
    k += vm_i64_to_str(name + k, f->num_locals++);
    name[k] = '\0';
    body_indent(f);
    body_cstr(f, "let ");
    body_cstr(f, name);
    if (type != VM_NULL) {
        body_cstr(f, ": ");
        body_cstr(f, type);
    }
    if (init >= 0) {
        const char *ti = val_text(f, init);
        if (ti == VM_NULL) return -1;
        body_cstr(f, " = ");
        body_cstr(f, ti);
    }
    body_cstr(f, "\n");
    const char *parts[1] = {name};
    return new_val(f, parts, 1);
}

VML_Result vml_b_assign(VML_FuncB *f, VML_Val target, VML_Val value) {
    const char *tt = val_text(f, target), *tv = val_text(f, value);
    if (tt == VM_NULL || tv == VM_NULL) return VML_ERR_STATE;
    body_indent(f);
    body_cstr(f, tt);
    body_cstr(f, " = ");
    body_cstr(f, tv);
    body_cstr(f, "\n");
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

VML_Result vml_b_eval(VML_FuncB *f, VML_Val v) {
    const char *t = val_text(f, v);
    if (t == VM_NULL) return VML_ERR_STATE;
    body_indent(f);
    body_cstr(f, t);
    body_cstr(f, "\n");
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

static VML_Result open_block(VML_FuncB *f, const char *kw, VML_Val cond) {
    const char *t = val_text(f, cond);
    if (t == VM_NULL) return VML_ERR_STATE;
    body_indent(f);
    body_cstr(f, kw);
    body_cstr(f, t);
    body_cstr(f, " {\n");
    f->depth++;
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

VML_Result vml_b_if(VML_FuncB *f, VML_Val cond)    { return open_block(f, "if ", cond); }
VML_Result vml_b_while(VML_FuncB *f, VML_Val cond) { return open_block(f, "while ", cond); }

VML_Result vml_b_else(VML_FuncB *f) {
    if (f == VM_NULL || f->depth == 0) {
        if (f) fb_fail(f, "vml_b_else: no open if");
        return VML_ERR_STATE;
    }
    f->depth--;
    body_indent(f);
    body_cstr(f, "} else {\n");
    f->depth++;
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

VML_Result vml_b_end(VML_FuncB *f) {
    if (f == VM_NULL || f->depth == 0) {
        if (f) fb_fail(f, "vml_b_end: no open block");
        return VML_ERR_STATE;
    }
    f->depth--;
    body_indent(f);
    body_cstr(f, "}\n");
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

VML_Result vml_b_return(VML_FuncB *f, VML_Val v) {
    const char *t = val_text(f, v);
    if (t == VM_NULL) return VML_ERR_STATE;
    body_indent(f);
    body_cstr(f, "return ");
    body_cstr(f, t);
    body_cstr(f, "\n");
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

VML_Result vml_b_return_void(VML_FuncB *f) {
    if (f == VM_NULL) return VML_ERR_STATE;
    body_indent(f);
    body_cstr(f, "return\n");
    return f->failed ? VML_ERR_MEMORY : VML_OK;
}

VML_Result vml_func_end(VML_FuncB *f) {
    if (f == VM_NULL) return VML_ERR_STATE;
    VML_Module *m = f->m;
    VML_Result res = VML_OK;
    if (f->depth != 0) fb_fail(f, "vml_func_end: an if / while block is still open (call vml_b_end)");
    if (f->failed) {
        res = VML_ERR_TYPE;
    } else {
        vml_src_append_cstr(m, f->header);
        vml_src_append_cstr(m, " {\n");
        if (f->body_len > 0) vml_src_append(m, f->body, f->body_len);
        if (!vml_src_append_cstr(m, "}\n\n")) res = VML_ERR_MEMORY;
    }
    for (vm_u32 i = 0; i < f->num_vals; i++) vml_mem(m->L, f->vals[i], f->val_len[i], 0);
    if (f->body) vml_mem(m->L, f->body, f->body_cap, 0);
    m->building = VM_FALSE;
    vml_mem(m->L, f, sizeof(VML_FuncB), 0);
    return res;
}

/* The generated declarations of a module (builder structs / enums / functions / typed wrappers) */
const char *vml_module_generated_source(VML_Module *m) {
    return (m != VM_NULL && m->src != VM_NULL) ? m->src : "";
}
