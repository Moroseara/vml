#include "context.h"

/*
 * Name index: hash buckets over chunk->functions (each chain ascending by fid, so the
 * first match is the lowest fid, as a linear scan would find) and over func_aliases
 * (each chain newest first). Functions are indexed lazily: fidx_sync catches up with
 * chunk->num_functions (growing or shrinking) before every lookup; a name changed in
 * place must be reported with c_func_index_rename.
 */
static vm_u32 name_hash(const char *s, vm_u32 len) {
    vm_u32 h = 2166136261u;
    for (vm_u32 i = 0; i < len; i++) h = (h ^ (vm_u8)s[i]) * 16777619u;
    return h;
}

static void fidx_unlink(Compiler *c, vm_u32 fid) {
    vm_i32 *link = &c->fidx_head[c->fidx_hash[fid] & (C_FIDX_BUCKETS - 1)];
    while (*link >= 0) {
        if (*link == (vm_i32)fid) { *link = c->fidx_next[fid]; return; }
        link = &c->fidx_next[*link];
    }
}

static void fidx_link(Compiler *c, vm_u32 fid) {
    const char *name = c->chunk->functions[fid].name;
    vm_u32 h = name_hash(name, vm_str_len(name));
    c->fidx_hash[fid] = h;
    vm_i32 *link = &c->fidx_head[h & (C_FIDX_BUCKETS - 1)];
    while (*link >= 0 && *link < (vm_i32)fid) link = &c->fidx_next[*link];
    c->fidx_next[fid] = *link;
    *link = (vm_i32)fid;
}

static void fidx_sync(Compiler *c) {
    while (c->fidx_upto > c->chunk->num_functions) fidx_unlink(c, --c->fidx_upto);
    while (c->fidx_upto < c->chunk->num_functions) fidx_link(c, c->fidx_upto++);
}

void c_func_index_reset(Compiler *c) {
    for (int i = 0; i < C_FIDX_BUCKETS; i++) c->fidx_head[i] = -1;
    for (int i = 0; i < C_AIDX_BUCKETS; i++) c->aidx_head[i] = -1;
    c->fidx_upto = 0;
}

void c_func_index_rename(Compiler *c, vm_u32 fid) {
    if (fid >= c->fidx_upto) return; /* not indexed yet; fidx_sync picks up the new name */
    fidx_unlink(c, fid);
    fidx_link(c, fid);
}

static vm_i32 fidx_first(Compiler *c, const char *name, vm_u32 len, vm_u32 *out_h) {
    fidx_sync(c);
    vm_u32 h = name_hash(name, len);
    *out_h = h;
    return c->fidx_head[h & (C_FIDX_BUCKETS - 1)];
}

static vm_bool fidx_is(Compiler *c, vm_i32 fid, vm_u32 h, const char *name, vm_u32 len) {
    if (c->fidx_hash[fid] != h) return VM_FALSE;
    const char *fname = c->chunk->functions[fid].name;
    return vm_str_eq(fname, vm_str_len(fname), name, len);
}

int c_func_first_named(Compiler *c, const char *name, vm_u32 len) {
    vm_u32 h;
    vm_i32 i = fidx_first(c, name, len, &h);
    while (i >= 0 && fidx_is(c, i, h, name, len) == VM_FALSE) i = c->fidx_next[i];
    return i;
}

/* `fid` must come from c_func_first_named / c_func_next_named with no function added or renamed since */
int c_func_next_named(Compiler *c, int fid, const char *name, vm_u32 len) {
    vm_u32 h = c->fidx_hash[fid];
    vm_i32 i = c->fidx_next[fid];
    while (i >= 0 && fidx_is(c, i, h, name, len) == VM_FALSE) i = c->fidx_next[i];
    return i;
}

static vm_i32 aidx_first(Compiler *c, vm_u32 h) {
    return c->aidx_head[h & (C_AIDX_BUCKETS - 1)];
}

static vm_bool aidx_is(Compiler *c, vm_i32 i, const char *name, vm_u32 len) {
    const char *aname = c->func_aliases[i].alias_name;
    return vm_str_eq(aname, vm_str_len(aname), name, len);
}

void c_register_alias(Compiler *c, const char *alias, vm_i32 func_id) {
    if (c->num_func_aliases >= 512) return;
    int idx = c->num_func_aliases++;
    FuncAlias *fa = &c->func_aliases[idx];
    vm_str_copy(fa->alias_name, alias, vm_str_len(alias), 64);
    fa->func_id = func_id;
    vm_i32 *head = &c->aidx_head[name_hash(fa->alias_name, vm_str_len(fa->alias_name)) & (C_AIDX_BUCKETS - 1)];
    c->aidx_next[idx] = *head;
    *head = idx;
}

/* Binds ALL overloaded functions matching `target_name` (or its aliases) to `new_alias` */
void c_bind_all_overloads(Compiler *c, const char *target_name, const char *new_alias) {
    vm_u32 tlen = vm_str_len(target_name);
    vm_u32 h;
    for (vm_i32 f = fidx_first(c, target_name, tlen, &h); f >= 0; f = c->fidx_next[f]) {
        if (fidx_is(c, f, h, target_name, tlen) == VM_TRUE) c_register_alias(c, new_alias, f);
    }
    /* Matching aliases that existed before this call, re-registered oldest first */
    vm_i32 found[512];
    int nfound = 0;
    for (vm_i32 i = aidx_first(c, h); i >= 0; i = c->aidx_next[i]) {
        if (aidx_is(c, i, target_name, tlen) == VM_TRUE) found[nfound++] = c->func_aliases[i].func_id;
    }
    while (nfound > 0) c_register_alias(c, new_alias, found[--nfound]);
}

int c_resolve_struct(Compiler *c, const char *name, vm_u32 len) {
    for (int i = 0; i < c->num_structs; i++) {
        vm_u32 slen = vm_str_len(c->structs[i].name);
        if (vm_str_eq(c->structs[i].name, slen, name, len) == VM_TRUE) return i;
    }
    return -1;
}

int c_resolve_type_contract(Compiler *c, const char *name, vm_u32 len) {
    for (int i = 0; i < c->num_type_contracts; i++) {
        vm_u32 tlen = vm_str_len(c->type_contracts[i].name);
        if (vm_str_eq(c->type_contracts[i].name, tlen, name, len) == VM_TRUE) return i;
    }
    return -1;
}

static vm_bool struct_has_method_sig(Compiler *c, int sid, const char *mname, vm_u32 mlen,
                                     vm_u8 arity, VMDataType ret_type, vm_bool ret_is_self) {
    if (sid < 0) return VM_FALSE;
    StructDef *sd = &c->structs[sid];
    char qname[64];
    vm_str_join_dot(qname, 64, sd->name, vm_str_len(sd->name), mname, mlen);
    vm_u32 qlen = vm_str_len(qname);
    VMDataType exp_ret = (ret_is_self == VM_TRUE && sd->num_fields > 0) ? sd->field_types[0] : ret_type;

    vm_u32 h;
    for (vm_i32 i = fidx_first(c, qname, qlen, &h); i >= 0; i = c->fidx_next[i]) {
        VMFunctionInfo *fn = &c->chunk->functions[i];
        if (fn->start_ip == 0xFFFFFFFFu) continue;
        if (fidx_is(c, i, h, qname, qlen) == VM_TRUE &&
            fn->arity == arity && fn->return_type == exp_ret) {
            return VM_TRUE;
        }
    }
    for (vm_u8 p = 0; p < sd->num_parents; p++) {
        if (struct_has_method_sig(c, sd->parent_struct_ids[p], mname, mlen, arity, ret_type, ret_is_self) == VM_TRUE) {
            return VM_TRUE;
        }
    }
    return VM_FALSE;
}

vm_bool c_does_struct_implement_type(Compiler *c, int sid, int tid) {
    if (sid < 0 || tid < 0) return VM_FALSE;
    StructDef *sd = &c->structs[sid];
    if (sd->owner_type_id == tid) return VM_TRUE;
    for (vm_u8 i = 0; i < sd->num_implemented_types; i++) {
        if (sd->implemented_types[i] == tid) return VM_TRUE;
    }
    for (vm_u8 p = 0; p < sd->num_parents; p++) {
        if (c_does_struct_implement_type(c, sd->parent_struct_ids[p], tid) == VM_TRUE) return VM_TRUE;
    }

    /* Implicit Structural Matching (Static Duck Typing) */
    TypeContract *tc = &c->type_contracts[tid];
    if (tc->is_alias == VM_TRUE || tc->is_func_type == VM_TRUE) return VM_FALSE;
    if (tc->num_req_fields == 0 && tc->num_req_methods == 0) return VM_FALSE;

    for (vm_u8 rf = 0; rf < tc->num_req_fields; rf++) {
        vm_bool found_f = VM_FALSE;
        vm_u32 rflen = vm_str_len(tc->req_field_names[rf]);
        for (vm_u8 sf = 0; sf < sd->num_fields; sf++) {
            vm_u32 sflen = vm_str_len(sd->field_names[sf]);
            if (vm_str_eq(sd->field_names[sf], sflen, tc->req_field_names[rf], rflen) == VM_TRUE &&
                sd->field_types[sf] == tc->req_field_types[rf]) {
                found_f = VM_TRUE;
                break;
            }
        }
        if (found_f == VM_FALSE) return VM_FALSE;
    }

    for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
        vm_u32 rmlen = vm_str_len(tc->req_method_names[rm]);
        if (struct_has_method_sig(c, sid, tc->req_method_names[rm], rmlen,
                                  tc->req_method_arity[rm], tc->req_method_ret[rm],
                                  tc->req_method_ret_is_self[rm]) == VM_FALSE) {
            return VM_FALSE;
        }
    }
    return VM_TRUE;
}

vm_bool c_is_struct_compatible(Compiler *c, int child_sid, int expected_sid) {
    if (expected_sid < 0) return VM_TRUE;
    if (child_sid < 0) return VM_FALSE;
    if (child_sid == expected_sid) return VM_TRUE;
    StructDef *sd = &c->structs[child_sid];
    for (vm_u8 p = 0; p < sd->num_parents; p++) {
        if (c_is_struct_compatible(c, sd->parent_struct_ids[p], expected_sid) == VM_TRUE) return VM_TRUE;
    }
    if (c->structs[expected_sid].owner_type_id >= 0) {
        if (c_does_struct_implement_type(c, child_sid, c->structs[expected_sid].owner_type_id) == VM_TRUE) {
            return VM_TRUE;
        }
    }
    if (sd->owner_type_id >= 0) {
        if (c_does_struct_implement_type(c, expected_sid, sd->owner_type_id) == VM_TRUE &&
            c->structs[expected_sid].num_fields == sd->num_fields) {
            return VM_TRUE;
        }
    }
    for (int t = 0; t < c->num_type_contracts; t++) {
        if (c->type_contracts[t].last_impl_struct_id == expected_sid ||
            c->type_contracts[t].layout_struct_id == expected_sid) {
            if (c_does_struct_implement_type(c, child_sid, t) == VM_TRUE) return VM_TRUE;
        }
    }
    return VM_FALSE;
}

vm_bool c_can_access_struct_member(Compiler *c, int owner_sid, vm_bool is_pub) {
    if (is_pub == VM_TRUE || owner_sid < 0) return VM_TRUE;
    if (c->current_struct_id >= 0 &&
        c_is_struct_compatible(c, c->current_struct_id, owner_sid) == VM_TRUE) {
        return VM_TRUE;
    }
    return VM_FALSE;
}

/*
 * Prevents C++ "Object Slicing" bug at compile-time:
 * Forbids assigning a derived struct with extra fields into a base struct variable slot.
 */
vm_bool c_check_no_object_slicing(Compiler *c, int dst_sid, int src_sid) {
    if (dst_sid < 0 || src_sid < 0 || dst_sid == src_sid) return VM_TRUE;
    if (c_is_struct_compatible(c, src_sid, dst_sid) == VM_TRUE) {
        if (c->structs[src_sid].num_fields > c->structs[dst_sid].num_fields) {
            c_error(c, "Object Slicing prevented: cannot slice derived struct into smaller base struct variable");
            return VM_FALSE;
        }
        return VM_TRUE;
    }
    c_error(c, "Incompatible struct types in assignment");
    return VM_FALSE;
}

/* Overloads of a name belong to the module that declared it first: another module (or the program)
   cannot add to them, so an imported package cannot catch calls meant for e.g. String.upper. */
vm_bool c_check_foreign_overload(Compiler *c, int new_fid) {
    VMFunctionInfo *nf = &c->chunk->functions[new_fid];
    vm_u32 nlen = vm_str_len(nf->name);
    int mine = c->func_module[new_fid];
    for (int i = c_func_first_named(c, nf->name, nlen); i >= 0 && i < new_fid; i = c_func_next_named(c, i, nf->name, nlen)) {
        int owner = c->func_module[i];
        if (owner == mine) continue;
        char msg[220];
        const char *owner_name = (owner >= 0) ? c->loaded_full[owner] : "the main program";
        const char *parts[] = {"Module cannot add overloads to ", nf->name, ": it belongs to ", owner_name,
                               " (only its owner can add overloads)"};
        vm_u32 n = 0;
        for (int k = 0; k < 5; k++) {
            vm_u32 pl = vm_str_len(parts[k]);
            for (vm_u32 j = 0; j < pl && n + 1 < sizeof(msg); j++) msg[n++] = parts[k][j];
        }
        msg[n] = '\0';
        c_error(c, msg);
        return VM_FALSE;
    }
    return VM_TRUE;
}

/* Detects duplicate function overloads with identical name and parameter types */
vm_bool c_check_duplicate_func_overload(Compiler *c, int new_fid) {
    VMFunctionInfo *nf = &c->chunk->functions[new_fid];
    vm_u32 nlen = vm_str_len(nf->name);
    vm_u32 h;
    for (vm_i32 i = fidx_first(c, nf->name, nlen, &h); i >= 0 && i < new_fid; i = c->fidx_next[i]) {
        VMFunctionInfo *ef = &c->chunk->functions[i];
        if (ef->start_ip == 0xFFFFFFFFu && nf->start_ip != 0xFFFFFFFFu) continue;
        if (fidx_is(c, i, h, nf->name, nlen) == VM_FALSE) continue;
        if (ef->arity != nf->arity) continue;
        vm_bool same = VM_TRUE;
        for (vm_u8 p = 0; p < nf->arity; p++) {
            if (ef->param_types[p] != nf->param_types[p] ||
                ef->param_struct_ids[p] != nf->param_struct_ids[p]) {
                same = VM_FALSE;
                break;
            }
        }
        if (same == VM_TRUE) {
            c_error(c, "Duplicate function overload with identical parameter types");
            return VM_FALSE;
        }
    }
    return VM_TRUE;
}

static vm_bool is_numeric_type(VMDataType t) {
    return vm_bool_from_int(t == VM_TYPE_I32 || t == VM_TYPE_I64 || t == VM_TYPE_F32 || t == VM_TYPE_F64);
}

/* numeric: also accept arguments that convert between i32 / i64 / f32 / f64 (the call converts them) */
static vm_bool sig_matches_ex(Compiler *c, int fid, const VMDataType *arg_types, const int *arg_sids, vm_u8 argc, vm_bool numeric) {
    VMFunctionInfo *fn = &c->chunk->functions[fid];
    if (argc < fn->min_arity || argc > fn->arity) return VM_FALSE;
    for (vm_u8 i = 0; i < argc; i++) {
        if (fn->coerce_args_to_string == VM_TRUE && i >= 1) continue;
        if (fn->param_struct_ids[i] >= 0) {
            StructDef *psd = &c->structs[fn->param_struct_ids[i]];
            if (psd->is_optional && arg_sids[i] < 0 &&
                (arg_types[i] == psd->opt_inner || arg_types[i] == VM_TYPE_VOID ||
                 (numeric && is_numeric_type(arg_types[i]) && is_numeric_type(psd->opt_inner)))) continue;
            if (c_is_struct_compatible(c, arg_sids[i], fn->param_struct_ids[i]) == VM_FALSE) return VM_FALSE;
        } else if (arg_sids[i] >= 0) {
            return VM_FALSE;
        } else if (fn->param_types[i] != arg_types[i]) {
            if (!(numeric && is_numeric_type(fn->param_types[i]) && is_numeric_type(arg_types[i]))) return VM_FALSE;
        }
    }
    return VM_TRUE;
}

static int resolve_overload_pass(Compiler *c, const char *name, vm_u32 len,
                                 const VMDataType *arg_types, const int *arg_sids, vm_u8 argc, vm_bool numeric) {
    vm_u32 h;
    for (vm_i32 i = fidx_first(c, name, len, &h); i >= 0; i = c->fidx_next[i]) {
        if (fidx_is(c, i, h, name, len) == VM_TRUE &&
            sig_matches_ex(c, (int)i, arg_types, arg_sids, argc, numeric) == VM_TRUE) {
            return (int)i;
        }
    }
    for (vm_i32 i = aidx_first(c, h); i >= 0; i = c->aidx_next[i]) {
        if (aidx_is(c, i, name, len) == VM_TRUE &&
            sig_matches_ex(c, c->func_aliases[i].func_id, arg_types, arg_sids, argc, numeric) == VM_TRUE) {
            return c->func_aliases[i].func_id;
        }
    }
    return -1;
}

/* Exact parameter types first; otherwise the first overload reachable by numeric conversion */
int c_resolve_func_overload(Compiler *c, const char *name, vm_u32 len,
                            const VMDataType *arg_types, const int *arg_sids, vm_u8 argc) {
    int fid = resolve_overload_pass(c, name, len, arg_types, arg_sids, argc, VM_FALSE);
    if (fid >= 0) return fid;
    return resolve_overload_pass(c, name, len, arg_types, arg_sids, argc, VM_TRUE);
}

int c_resolve_func(Compiler *c, const char *name, vm_u32 len) {
    vm_u32 h;
    for (vm_i32 i = fidx_first(c, name, len, &h); i >= 0; i = c->fidx_next[i]) {
        if (fidx_is(c, i, h, name, len) == VM_TRUE) return (int)i;
    }
    for (vm_i32 i = aidx_first(c, h); i >= 0; i = c->aidx_next[i]) {
        if (aidx_is(c, i, name, len) == VM_TRUE) return c->func_aliases[i].func_id;
    }
    return -1;
}

int c_resolve_struct_method_overload(Compiler *c, int sid, const char *mname, vm_u32 mlen,
                                     const VMDataType *arg_types, const int *arg_sids, vm_u8 argc) {
    if (sid < 0) return -1;
    char qname[64];
    vm_str_join_dot(qname, 64, c->structs[sid].name, vm_str_len(c->structs[sid].name), mname, mlen);
    int fid = c_resolve_func_overload(c, qname, vm_str_len(qname), arg_types, arg_sids, argc);
    if (fid >= 0 && c->chunk->functions[fid].start_ip != 0xFFFFFFFFu) return fid;
    StructDef *sd = &c->structs[sid];
    for (vm_u8 p = 0; p < sd->num_parents; p++) {
        int pfid = c_resolve_struct_method_overload(c, sd->parent_struct_ids[p], mname, mlen, arg_types, arg_sids, argc);
        if (pfid >= 0) return pfid;
    }
    if (sd->owner_type_id >= 0) {
        int tid = sd->owner_type_id;
        int impl_sid = c->type_contracts[tid].last_impl_struct_id;
        if (impl_sid >= 0 && impl_sid != sid) {
            int ifid = c_resolve_struct_method_overload(c, impl_sid, mname, mlen, arg_types, arg_sids, argc);
            if (ifid >= 0) return ifid;
        }
        for (int s = 0; s < c->num_structs; s++) {
            if (s == sid || c->structs[s].owner_type_id >= 0) continue;
            if (c_does_struct_implement_type(c, s, tid) == VM_TRUE) {
                int sfid = c_resolve_struct_method_overload(c, s, mname, mlen, arg_types, arg_sids, argc);
                if (sfid >= 0) return sfid;
            }
        }
    }
    return fid;
}

int c_resolve_struct_method(Compiler *c, int sid, const char *mname, vm_u32 mlen) {
    if (sid < 0) return -1;
    char qname[64];
    vm_str_join_dot(qname, 64, c->structs[sid].name, vm_str_len(c->structs[sid].name), mname, mlen);
    int fid = c_resolve_func(c, qname, vm_str_len(qname));
    if (fid >= 0 && c->chunk->functions[fid].start_ip != 0xFFFFFFFFu) return fid;
    StructDef *sd = &c->structs[sid];
    for (vm_u8 p = 0; p < sd->num_parents; p++) {
        int pfid = c_resolve_struct_method(c, sd->parent_struct_ids[p], mname, mlen);
        if (pfid >= 0) return pfid;
    }
    if (sd->owner_type_id >= 0) {
        int tid = sd->owner_type_id;
        int impl_sid = c->type_contracts[tid].last_impl_struct_id;
        if (impl_sid >= 0 && impl_sid != sid) {
            int ifid = c_resolve_struct_method(c, impl_sid, mname, mlen);
            if (ifid >= 0) return ifid;
        }
        for (int s = 0; s < c->num_structs; s++) {
            if (s == sid || c->structs[s].owner_type_id >= 0) continue;
            if (c_does_struct_implement_type(c, s, tid) == VM_TRUE) {
                int sfid = c_resolve_struct_method(c, s, mname, mlen);
                if (sfid >= 0) return sfid;
            }
        }
    }
    return fid;
}

int c_resolve_enum_variant(Compiler *c, const char *qname, vm_u32 len) {
    for (int i = 0; i < c->num_enum_variants; i++) {
        vm_u32 vlen = vm_str_len(c->enum_variants[i].qname);
        if (vm_str_eq(c->enum_variants[i].qname, vlen, qname, len) == VM_TRUE) return i;
    }
    return -1;
}

int c_resolve_generic(Compiler *c, const char *name, vm_u32 len) {
    for (int i = 0; i < c->num_generics; i++) {
        vm_u32 glen = vm_str_len(c->generics[i].name);
        if (vm_str_eq(c->generics[i].name, glen, name, len) == VM_TRUE) return i;
    }
    return -1;
}

void c_verify_all_type_contracts(Compiler *c) {
    for (int s = 0; s < c->num_structs; s++) {
        StructDef *sd = &c->structs[s];
        if (sd->owner_type_id >= 0) continue;
        for (vm_u8 t = 0; t < sd->num_implemented_types; t++) {
            TypeContract *tc = &c->type_contracts[sd->implemented_types[t]];
            for (vm_u8 rm = 0; rm < tc->num_req_methods; rm++) {
                vm_u32 rmlen = vm_str_len(tc->req_method_names[rm]);
                if (struct_has_method_sig(c, s, tc->req_method_names[rm], rmlen,
                                          tc->req_method_arity[rm], tc->req_method_ret[rm],
                                          tc->req_method_ret_is_self[rm]) == VM_FALSE) {
                    c_error(c, "Struct is missing a required method (or has mismatched signature) from bound 'type' contract");
                    return;
                }
            }
        }
    }
}

vm_u32 c_alloc_func_slot(Compiler *c) {
    if (c->chunk->num_functions >= VM_MAX_FUNCTIONS) {
        c_error(c, "Too many functions in program (limit reached)");
        return VM_MAX_FUNCTIONS - 1; /* scratch slot; compilation already failed */
    }
    fidx_sync(c); /* drop any stale entry for a slot freed by num_functions-- */
    c->func_module[c->chunk->num_functions] = (vm_i8)c->cur_module_idx;
    return c->chunk->num_functions++;
}
