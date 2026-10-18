#include "vm_gc.h"
#include "../cpu/vm_cpu.h"

enum { K_ARRAY = 0, K_MAP = 1, K_CELL = 2, K_CLOSURE = 3 };

typedef struct {
    VMChunk *chunk;
    vm_u8   *arrays, *maps, *strings, *cells, *closures;
    vm_u32  *stack;
    vm_u32   sp, cap;
    vm_bool  oom;
} Marker;

void vm_gc_seal(VMChunk *chunk) {
    if (chunk->gc_sealed) return;
    chunk->sealed_strings = chunk->num_strings;
    chunk->sealed_arena = chunk->string_arena_used;
    chunk->gc_sealed = VM_TRUE;
}

void vm_gc_push_cpu(VMChunk *chunk, void *cpu) {
    if (chunk->gc_num_cpus < VM_GC_MAX_CPUS) chunk->gc_cpus[chunk->gc_num_cpus] = cpu;
    chunk->gc_num_cpus++;
}

void vm_gc_pop_cpu(VMChunk *chunk) {
    if (chunk->gc_num_cpus > 0) chunk->gc_num_cpus--;
}

/* ---------------------------------------------------------------------------
 * Mark
 * ------------------------------------------------------------------------- */

static void push(Marker *m, vm_u32 kind, vm_u32 id) {
    if (m->sp >= m->cap) {
        vm_u32 nc = (m->cap > 0) ? m->cap * 2 : 256;
        vm_u32 *ns = (vm_u32 *)vm_chunk_realloc(m->chunk, m->stack, sizeof(vm_u32) * m->cap, sizeof(vm_u32) * nc);
        if (ns == VM_NULL) {
            m->oom = VM_TRUE;
            return;
        }
        m->stack = ns;
        m->cap = nc;
    }
    m->stack[m->sp++] = (kind << 28) | id;
}

static inline void mark_string(Marker *m, vm_u32 sid) {
    if (sid < m->chunk->num_strings) m->strings[sid] = 1;
}

static inline void mark_obj(Marker *m, vm_u32 kind, vm_u32 id) {
    VMChunk *c = m->chunk;
    vm_u8 *bits;
    switch (kind) {
        case K_ARRAY:   if (id >= c->num_arrays || c->arrays[id].dead) return;     bits = m->arrays; break;
        case K_MAP:     if (id >= c->num_maps || c->maps[id].dead) return;         bits = m->maps; break;
        case K_CELL:    if (id >= c->num_cells) return;                            bits = m->cells; break;
        default:        if (id >= c->num_closures || c->closures[id].dead) return; bits = m->closures; break;
    }
    if (bits[id]) return;
    bits[id] = 1;
    push(m, kind, id);
}

/* Untyped value: could be any id (or a tagged closure) */
static void mark_conservative(Marker *m, VMReg r) {
    vm_u32 v = (vm_u32)r.i32;
    if (VM_IS_CLOSURE(v)) {
        mark_obj(m, K_CLOSURE, v & ~(vm_u32)VM_CLOSURE_TAG);
        return;
    }
    mark_string(m, v);
    mark_obj(m, K_ARRAY, v);
    mark_obj(m, K_MAP, v);
    mark_obj(m, K_CELL, v);
}

/* Value whose static type is known. I32 slots may hold function values (closures). */
static void mark_typed(Marker *m, VMDataType t, VMReg r) {
    switch (t) {
        case VM_TYPE_STRING: mark_string(m, r.str_id); break;
        case VM_TYPE_ARRAY:  mark_obj(m, K_ARRAY, r.arr_id); break;
        case VM_TYPE_MAP:    mark_obj(m, K_MAP, r.map_id); break;
        case VM_TYPE_I32:
            if (VM_IS_CLOSURE((vm_u32)r.i32)) mark_obj(m, K_CLOSURE, (vm_u32)r.i32 & ~(vm_u32)VM_CLOSURE_TAG);
            break;
        default: break;
    }
}

/* Containers whose element type was unknown when created (`[]`, `{:}`) carry VOID: scan conservatively */
static void mark_element(Marker *m, VMDataType t, VMReg r) {
    if (t == VM_TYPE_VOID || t >= VM_TYPE_COUNT) mark_conservative(m, r);
    else mark_typed(m, t, r);
}

static void drain(Marker *m) {
    VMChunk *c = m->chunk;
    while (m->sp > 0) {
        vm_u32 top = m->stack[--m->sp];
        vm_u32 kind = top >> 28;
        vm_u32 id = top & 0x0FFFFFFFu;
        if (kind == K_ARRAY) {
            const VMArray *a = &c->arrays[id];
            for (vm_u32 i = 0; i < a->length; i++) mark_element(m, a->elem_type, a->items[i]);
        } else if (kind == K_MAP) {
            const VMMap *mp = &c->maps[id];
            for (vm_u32 i = 0; i < mp->length; i++) {
                mark_string(m, mp->keys[i]);
                mark_element(m, mp->val_type, mp->vals[i]);
            }
        } else if (kind == K_CELL) {
            mark_element(m, (VMDataType)c->cell_types[id], c->cells[id]);
        } else {
            const VMClosure *cl = &c->closures[id];
            for (vm_u8 k = 0; k < cl->num_cells; k++) mark_obj(m, K_CELL, c->closure_cells[cl->first_cell + k]);
        }
    }
}

static void mark_roots(Marker *m) {
    VMChunk *c = m->chunk;
    for (vm_u32 i = 0; i < c->sealed_strings && i < c->num_strings; i++) m->strings[i] = 1;
    mark_string(m, c->last_error_str_id);
    mark_string(m, c->last_traceback_str_id);

    vm_u32 ncpu = (c->gc_num_cpus < VM_GC_MAX_CPUS) ? c->gc_num_cpus : VM_GC_MAX_CPUS;
    for (vm_u32 k = 0; k < ncpu; k++) {
        const VMCPU *cpu = (const VMCPU *)c->gc_cpus[k];
        for (vm_u32 r = 0; r < VM_MAX_REGISTERS; r++) mark_conservative(m, cpu->registers[r]);
        if (cpu->cur_closure >= 0) mark_obj(m, K_CLOSURE, (vm_u32)cpu->cur_closure);
        for (vm_u32 f = 0; f < cpu->frame_count; f++) {
            if (cpu->frames[f].prev_closure >= 0) mark_obj(m, K_CLOSURE, (vm_u32)cpu->frames[f].prev_closure);
            if (cpu->frames[f].prev_env_map_id >= 0) mark_obj(m, K_MAP, (vm_u32)cpu->frames[f].prev_env_map_id);
        }
        mark_string(m, cpu->last_error_str_id);
    }

    for (vm_u32 i = 0; i < c->num_constants; i++) mark_typed(m, c->constant_types[i], c->constants[i]);
    for (vm_u32 i = 0; i < c->num_mod_consts; i++) mark_typed(m, c->mod_consts[i].type, c->mod_consts[i].val);
    for (vm_u32 s = 0; s < c->num_mod_structs; s++) {
        const VMModuleStruct *ms = &c->mod_structs[s];
        for (vm_u8 f = 0; f < ms->num_fields && f < 4; f++) {
            if (ms->field_has_default[f]) mark_typed(m, ms->field_types[f], ms->field_defaults[f]);
        }
    }
    for (vm_u32 f = 0; f < c->num_functions; f++) {
        const VMFunctionInfo *fn = &c->functions[f];
        for (vm_u8 p = 0; p < fn->arity && p < VM_MAX_PARAMS; p++) {
            if (fn->param_has_default[p]) mark_typed(m, fn->param_types[p], fn->param_defaults[p]);
        }
        for (int u = 0; u < 8; u++) mark_conservative(m, fn->upvalues[u]);
        if (fn->env_map_id >= 0) mark_obj(m, K_MAP, (vm_u32)fn->env_map_id);
    }
    if (c->active_env_map_id >= 0) mark_obj(m, K_MAP, (vm_u32)c->active_env_map_id);
    for (vm_u32 t = 0; t < c->num_tasks; t++) {
        const VMTaskEntry *te = &c->tasks[t];
        mark_conservative(m, te->arg);
        mark_conservative(m, te->result);
        VMReg fv = {0};
        fv.i32 = te->func_id;
        mark_conservative(m, fv);
        mark_string(m, te->error_str_id);
    }
}

/* ---------------------------------------------------------------------------
 * Sweep
 * ------------------------------------------------------------------------- */

static void free_list_reset(VMFreeList *fl) {
    fl->count = 0;
}

static vm_bool free_list_push(VMChunk *c, VMFreeList *fl, vm_u32 id) {
    if (fl->count >= fl->cap) {
        vm_u32 nc = (fl->cap > 0) ? fl->cap * 2 : 64;
        vm_u32 *ni = (vm_u32 *)vm_chunk_realloc(c, fl->ids, sizeof(vm_u32) * fl->cap, sizeof(vm_u32) * nc);
        if (ni == VM_NULL) return VM_FALSE;
        fl->ids = ni;
        fl->cap = nc;
    }
    fl->ids[fl->count++] = id;
    return VM_TRUE;
}

static vm_bool free_list_reserve(VMChunk *c, VMFreeList *fl, vm_u32 n) {
    if (n <= fl->cap) return VM_TRUE;
    vm_u32 nc = (fl->cap > 0) ? fl->cap : 64;
    while (nc < n) nc *= 2;
    vm_u32 *ni = (vm_u32 *)vm_chunk_realloc(c, fl->ids, sizeof(vm_u32) * fl->cap, sizeof(vm_u32) * nc);
    if (ni == VM_NULL) return VM_FALSE;
    fl->ids = ni;
    fl->cap = nc;
    return VM_TRUE;
}

/*
 * One pass over runtime strings: live ones are copied into a fresh arena (permanent
 * bytes keep their offsets), dead ones go on the free list.
 */
static void sweep_strings(VMChunk *c, const vm_u8 *marked) {
    vm_u64 lo = (vm_u64)c->string_arena;
    vm_u64 hi = lo + c->string_arena_used;
    vm_u32 live_bytes = 0;
    for (vm_u32 i = c->sealed_strings; i < c->num_strings; i++) {
        if (marked[i]) live_bytes += c->strings[i].length + 1;
    }
    vm_u32 need = c->sealed_arena + live_bytes;
    /* Keep the current capacity (the program will refill it before the next collection);
     * shrink only when it is mostly empty. Re-growing would rebase every string again. */
    vm_u32 new_cap = c->string_arena_cap;
    if (new_cap < VM_INIT_STRING_ARENA) new_cap = VM_INIT_STRING_ARENA;
    while (new_cap < need + need / 2 + 1) new_cap *= 2;
    while (new_cap > VM_INIT_STRING_ARENA && new_cap / 8 > need) new_cap /= 2;
    char *arena = (char *)vm_chunk_realloc(c, VM_NULL, 0, new_cap);
    if (arena == VM_NULL) return;   /* out of memory: keep everything as is */
    if (free_list_reserve(c, &c->free_strings, c->num_strings - c->sealed_strings) == VM_FALSE) {
        vm_chunk_realloc(c, arena, new_cap, 0);
        return;
    }

    for (vm_u32 i = 0; i < c->sealed_arena; i++) arena[i] = c->string_arena[i];
    /* Permanent strings with bytes in the kept prefix move with the arena */
    for (vm_u32 i = 0; i < c->sealed_strings; i++) {
        vm_u64 p = (vm_u64)c->strings[i].chars;
        if (p >= lo && p < hi) c->strings[i].chars = arena + (p - lo);
    }

    vm_u32 cursor = c->sealed_arena;
    vm_u32 nfree = 0;
    vm_u32 *free_ids = c->free_strings.ids;
    for (vm_u32 i = c->sealed_strings; i < c->num_strings; i++) {
        VMString *s = &c->strings[i];
        if (!marked[i]) {
            s->chars = "";
            s->length = 0;
            c->string_dead[i] = VM_STR_DEAD;
            free_ids[nfree++] = i;
            continue;
        }
        vm_u64 p = (vm_u64)s->chars;
        if (p < lo || p >= hi) continue;          /* bytes live outside the arena (C literal) */
        vm_u32 len = s->length;
        char *dst = arena + cursor;
        for (vm_u32 k = 0; k < len; k++) dst[k] = s->chars[k];
        dst[len] = '\0';
        s->chars = dst;
        cursor += len + 1;
    }
    c->free_strings.count = nfree;
    vm_chunk_realloc(c, c->string_arena, c->string_arena_cap, 0);
    c->string_arena = arena;
    c->string_arena_cap = new_cap;
    c->string_arena_used = cursor;
}

static void compact_closure_cells(VMChunk *c) {
    vm_u32 total = 0;
    for (vm_u32 i = 0; i < c->num_closures; i++) if (!c->closures[i].dead) total += c->closures[i].num_cells;
    vm_u32 cap = 64;
    while (cap < total + 1) cap *= 2;
    vm_u32 *cells = (vm_u32 *)vm_chunk_realloc(c, VM_NULL, 0, sizeof(vm_u32) * cap);
    if (cells == VM_NULL) return;
    vm_u32 cursor = 0;
    for (vm_u32 i = 0; i < c->num_closures; i++) {
        VMClosure *cl = &c->closures[i];
        if (cl->dead) continue;
        for (vm_u8 k = 0; k < cl->num_cells; k++) cells[cursor + k] = c->closure_cells[cl->first_cell + k];
        cl->first_cell = cursor;
        cursor += cl->num_cells;
    }
    vm_chunk_realloc(c, c->closure_cells, sizeof(vm_u32) * c->closure_cells_cap, 0);
    c->closure_cells = cells;
    c->closure_cells_cap = cap;
    c->num_closure_cells = cursor;
}

void vm_gc_collect(VMChunk *c) {
    c->gc_pending = VM_FALSE;
    if (c->gc_disabled || !c->gc_sealed) return;

    Marker m;
    m.chunk = c;
    m.stack = VM_NULL;
    m.sp = m.cap = 0;
    m.oom = VM_FALSE;
    vm_u32 n_total = c->num_arrays + c->num_maps + c->num_strings + c->num_cells + c->num_closures + 5;
    vm_u8 *bits = (vm_u8 *)vm_chunk_realloc(c, VM_NULL, 0, n_total);
    if (bits == VM_NULL) return;
    for (vm_u32 i = 0; i < n_total; i++) bits[i] = 0;
    m.arrays = bits;
    m.maps = m.arrays + c->num_arrays + 1;
    m.strings = m.maps + c->num_maps + 1;
    m.cells = m.strings + c->num_strings + 1;
    m.closures = m.cells + c->num_cells + 1;

    mark_roots(&m);
    drain(&m);
    vm_chunk_realloc(c, m.stack, sizeof(vm_u32) * m.cap, 0);
    if (m.oom) {
        /* Incomplete mark: freeing anything now could be wrong */
        vm_chunk_realloc(c, bits, n_total, 0);
        return;
    }

    vm_u64 live = 0;

    free_list_reset(&c->free_arrays);
    for (vm_u32 i = 0; i < c->num_arrays; i++) {
        VMArray *a = &c->arrays[i];
        if (m.arrays[i]) {
            live += sizeof(VMArray) + (vm_u64)a->capacity * sizeof(VMReg);
            continue;
        }
        if (!a->dead) {
            vm_chunk_realloc(c, a->items, sizeof(VMReg) * a->capacity, 0);
            a->items = VM_NULL;
            a->capacity = 0;
            a->length = 0;
            a->dead = VM_TRUE;
        }
        free_list_push(c, &c->free_arrays, i);
    }

    free_list_reset(&c->free_maps);
    for (vm_u32 i = 0; i < c->num_maps; i++) {
        VMMap *mp = &c->maps[i];
        if (m.maps[i]) {
            live += sizeof(VMMap) + (vm_u64)mp->capacity * (sizeof(VMReg) + sizeof(vm_u32));
            continue;
        }
        if (!mp->dead) {
            vm_chunk_realloc(c, mp->keys, sizeof(vm_u32) * mp->capacity, 0);
            vm_chunk_realloc(c, mp->vals, sizeof(VMReg) * mp->capacity, 0);
            mp->keys = VM_NULL;
            mp->vals = VM_NULL;
            mp->capacity = 0;
            mp->length = 0;
            mp->dead = VM_TRUE;
        }
        free_list_push(c, &c->free_maps, i);
    }

    free_list_reset(&c->free_cells);
    for (vm_u32 i = 0; i < c->num_cells; i++) {
        if (m.cells[i]) live += sizeof(VMReg);
        else free_list_push(c, &c->free_cells, i);
    }

    free_list_reset(&c->free_closures);
    for (vm_u32 i = 0; i < c->num_closures; i++) {
        if (m.closures[i]) {
            live += sizeof(VMClosure) + c->closures[i].num_cells * 4u;
            continue;
        }
        c->closures[i].dead = VM_TRUE;
        c->closures[i].num_cells = 0;
        free_list_push(c, &c->free_closures, i);
    }
    compact_closure_cells(c);

    sweep_strings(c, m.strings);
    vm_chunk_intern_rebuild(c);
    live += c->string_arena_used + (vm_u64)c->num_strings * 24u;

    vm_chunk_realloc(c, bits, n_total, 0);
    c->gc_allocated = 0;
    if (c->gc_threshold != 1) {   /* threshold 1 = stress mode (`--gc-stress`): collect at every safepoint */
        c->gc_threshold = (live * 2 > VM_GC_MIN_THRESHOLD) ? live * 2 : VM_GC_MIN_THRESHOLD;
    }
    c->gc_collections++;
}
