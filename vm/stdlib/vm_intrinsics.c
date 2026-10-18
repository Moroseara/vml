#include "vm_intrinsics.h"
#include "../string/vm_string.h"
#include "../syscall/vm_syscall.h"
#include "vm_stdlib.h"

/* ---------------------------------------------------------------------------
 * Strings / bytes
 * ------------------------------------------------------------------------- */

/* Copies a VM string into a NUL-terminated buffer (stack buffer when it fits) */
static char *copy_cstr(VMChunk *chunk, vm_u32 sid, char *stack_buf, vm_u32 cap, vm_u32 *out_len) {
    VMString s = chunk->strings[sid];
    char *buf = (s.length + 1 <= cap) ? stack_buf : (char *)vm_sys_default_realloc(VM_NULL, VM_NULL, 0, s.length + 1);
    if (buf == VM_NULL) return VM_NULL;
    for (vm_u32 i = 0; i < s.length; i++) buf[i] = s.chars[i];
    buf[s.length] = '\0';
    if (out_len != VM_NULL) *out_len = s.length;
    return buf;
}

static void free_cstr(char *buf, const char *stack_buf, vm_u32 len) {
    if (buf != VM_NULL && buf != stack_buf) vm_sys_default_realloc(VM_NULL, buf, len + 1, 0);
}

/* str_skip_ws(s, i): i32 — first index >= i that is not space/tab/CR/LF (length when none) */
static VMReg n_str_skip_ws(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_u32 i = args[1].i32 < 0 ? 0 : (vm_u32)args[1].i32;
    while (i < s.length) {
        char c = s.chars[i];
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
        i++;
    }
    out.i32 = (vm_i32)(i < s.length ? i : s.length);
    return out;
}

/* str_scan_plain(s, i): i32 — first index >= i holding '"', '\\' or a control byte (length when none) */
static VMReg n_str_scan_plain(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_u32 i = args[1].i32 < 0 ? 0 : (vm_u32)args[1].i32;
    while (i < s.length) {
        unsigned char c = (unsigned char)s.chars[i];
        if (c == '"' || c == '\\' || c < 32) break;
        i++;
    }
    out.i32 = (vm_i32)(i < s.length ? i : s.length);
    return out;
}

/* str_span_num(s, i): i32 — end of the run of number bytes (0-9 + - . e E) starting at i */
static VMReg n_str_span_num(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_u32 i = args[1].i32 < 0 ? 0 : (vm_u32)args[1].i32;
    while (i < s.length) {
        char c = s.chars[i];
        if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
        i++;
    }
    out.i32 = (vm_i32)(i < s.length ? i : s.length);
    return out;
}

/* str_byte(s, i): i32 — byte at i, or -1 when out of range */
static VMReg n_str_byte(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_i32 i = args[1].i32;
    out.i32 = (i >= 0 && (vm_u32)i < s.length) ? (vm_i32)(unsigned char)s.chars[i] : -1;
    return out;
}

/* str_char(b): string — one-byte string */
static VMReg n_str_char(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    char c = (char)(args[0].i32 & 0xFF);
    out.str_id = vm_chunk_new_string(chunk, &c, 1);
    return out;
}

/* str_find_from(s, needle, from): i32 — first index >= from, or -1 */
static VMReg n_str_find_from(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    VMString n = chunk->strings[args[1].str_id];
    vm_i32 from = args[2].i32 < 0 ? 0 : args[2].i32;
    out.i32 = -1;
    if (n.length == 0) {
        out.i32 = ((vm_u32)from <= s.length) ? from : -1;
        return out;
    }
    if (n.length > s.length) return out;
    for (vm_u32 i = (vm_u32)from; i + n.length <= s.length; i++) {
        if (s.chars[i] == n.chars[0] && vm_str_eq(s.chars + i, n.length, n.chars, n.length) == VM_TRUE) {
            out.i32 = (vm_i32)i;
            return out;
        }
    }
    return out;
}

/* str_repeat(s, n): string — built in one allocation */
static VMReg n_str_repeat(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    vm_i32 n = args[1].i32;
    vm_u32 len = chunk->strings[args[0].str_id].length;
    if (n <= 0 || len == 0) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    vm_u32 total = len * (vm_u32)n;
    char *dst = vm_chunk_arena_reserve(chunk, total);
    if (dst == VM_NULL) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    const char *src = chunk->strings[args[0].str_id].chars;   /* read after reserve: the arena may move */
    for (vm_i32 k = 0; k < n; k++) {
        for (vm_u32 i = 0; i < len; i++) dst[(vm_u32)k * len + i] = src[i];
    }
    dst[total] = '\0';
    out.str_id = vm_chunk_commit_string(chunk, dst, total);
    return out;
}

/* str_hash(s): i64 — FNV-1a */
static VMReg n_str_hash(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_u64 h = 1469598103934665603ull;
    for (vm_u32 i = 0; i < s.length; i++) {
        h ^= (unsigned char)s.chars[i];
        h *= 1099511628211ull;
    }
    out.i64 = (vm_i64)h;
    return out;
}

/* str_cmp(a, b): i32 — <0, 0, >0 (byte order) */
static VMReg n_str_cmp(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString a = chunk->strings[args[0].str_id];
    VMString b = chunk->strings[args[1].str_id];
    vm_u32 n = (a.length < b.length) ? a.length : b.length;
    for (vm_u32 i = 0; i < n; i++) {
        int d = (int)(unsigned char)a.chars[i] - (int)(unsigned char)b.chars[i];
        if (d != 0) {
            out.i32 = d;
            return out;
        }
    }
    out.i32 = (a.length < b.length) ? -1 : (a.length > b.length) ? 1 : 0;
    return out;
}

/* parse_f64(s): f64 — decimal with optional fraction and exponent ("-1.5e3"); 0 when invalid */
static VMReg n_parse_f64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_u32 i = 0;
    vm_bool neg = VM_FALSE;
    if (i < s.length && (s.chars[i] == '-' || s.chars[i] == '+')) neg = vm_bool_from_int(s.chars[i++] == '-');
    vm_f64 v = 0.0;
    while (i < s.length && s.chars[i] >= '0' && s.chars[i] <= '9') v = v * 10.0 + (vm_f64)(s.chars[i++] - '0');
    if (i < s.length && s.chars[i] == '.') {
        i++;
        vm_f64 scale = 0.1;
        while (i < s.length && s.chars[i] >= '0' && s.chars[i] <= '9') {
            v += (vm_f64)(s.chars[i++] - '0') * scale;
            scale *= 0.1;
        }
    }
    if (i < s.length && (s.chars[i] == 'e' || s.chars[i] == 'E')) {
        i++;
        vm_bool eneg = VM_FALSE;
        if (i < s.length && (s.chars[i] == '-' || s.chars[i] == '+')) eneg = vm_bool_from_int(s.chars[i++] == '-');
        int e = 0;
        while (i < s.length && s.chars[i] >= '0' && s.chars[i] <= '9' && e < 400) e = e * 10 + (s.chars[i++] - '0');
        vm_f64 p = 1.0, base = 10.0;
        while (e > 0) {
            if (e & 1) p *= base;
            base *= base;
            e >>= 1;
        }
        v = eneg ? v / p : v * p;
    }
    out.f64 = neg ? -v : v;
    return out;
}

/* ---------------------------------------------------------------------------
 * Arrays
 * ------------------------------------------------------------------------- */

static VMReg n_arr_clear(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id < chunk->num_arrays) chunk->arrays[args[0].arr_id].length = 0;
    return out;
}

/* arr_join(a, sep): string — any element type (converted like `as string`); one allocation, linear time */
static VMReg n_arr_join(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id >= chunk->num_arrays || chunk->arrays[args[0].arr_id].length == 0) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    vm_u32 arr = args[0].arr_id;
    vm_u32 sep = args[1].str_id;
    vm_u32 n = chunk->arrays[arr].length;
    VMDataType et = chunk->arrays[arr].elem_type;
    vm_u8 epair = (vm_u8)(((vm_u8)et << 4) | ((vm_u8)VM_TYPE_STRING & 0x0Fu));
    /* string ids of every element (non-strings are converted first) */
    vm_u32 *ids = (vm_u32 *)vm_sys_default_realloc(VM_NULL, VM_NULL, 0, sizeof(vm_u32) * n);
    if (ids == VM_NULL) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    vm_u64 total = 0;
    for (vm_u32 i = 0; i < n; i++) {
        VMReg item = chunk->arrays[arr].items[i];
        ids[i] = (et == VM_TYPE_STRING) ? item.str_id : vm_chunk_cast_value(chunk, item, epair).str_id;
        total += chunk->strings[ids[i]].length;
    }
    vm_u32 slen = chunk->strings[sep].length;
    total += (vm_u64)slen * (n - 1);
    if (total > 0x7FFFFFFFu) total = 0;
    char *dst = vm_chunk_arena_reserve(chunk, (vm_u32)total);
    if (dst == VM_NULL) {
        vm_sys_default_realloc(VM_NULL, ids, sizeof(vm_u32) * n, 0);
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    vm_u32 k = 0;
    for (vm_u32 i = 0; i < n && k < total; i++) {
        if (i > 0) {
            const char *sp = chunk->strings[sep].chars;   /* re-read: reserve may have moved the arena */
            for (vm_u32 c = 0; c < slen; c++) dst[k++] = sp[c];
        }
        VMString piece = chunk->strings[ids[i]];
        for (vm_u32 c = 0; c < piece.length; c++) dst[k++] = piece.chars[c];
    }
    dst[k] = '\0';
    out.str_id = vm_chunk_commit_string(chunk, dst, k);
    vm_sys_default_realloc(VM_NULL, ids, sizeof(vm_u32) * n, 0);
    return out;
}

/* ---------------------------------------------------------------------------
 * Math (f64)
 * ------------------------------------------------------------------------- */

static vm_f64 f64_floor(vm_f64 x) {
    if (x >= 9.0e18 || x <= -9.0e18 || x != x) return x;
    vm_f64 t = (vm_f64)(vm_i64)x;
    return (t > x) ? t - 1.0 : t;
}

static VMReg n_sqrt64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)argc;
    VMReg out = {0};
    vm_f64 x = args[0].f64;
    if (x <= 0.0) return out;
    vm_f64 g = (x > 1.0) ? x * 0.5 : 1.0;
    for (int i = 0; i < 64; i++) {
        vm_f64 n = 0.5 * (g + x / g);
        if (n == g) break;
        g = n;
    }
    out.f64 = g;
    return out;
}

static VMReg n_floor64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)argc;
    VMReg out = {0};
    out.f64 = f64_floor(args[0].f64);
    return out;
}

static VMReg n_ceil64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)argc;
    VMReg out = {0};
    out.f64 = -f64_floor(-args[0].f64);
    return out;
}

/* sin with range reduction to [-pi, pi] and a Taylor series */
static vm_f64 f64_sin(vm_f64 x) {
    const vm_f64 two_pi = 6.283185307179586;
    x = x - two_pi * f64_floor((x + 3.141592653589793) / two_pi);
    vm_f64 term = x, sum = x, x2 = x * x;
    for (int n = 1; n < 12; n++) {
        term *= -x2 / (vm_f64)((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

static VMReg n_sin64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)argc;
    VMReg out = {0};
    out.f64 = f64_sin(args[0].f64);
    return out;
}

static VMReg n_cos64(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)argc;
    VMReg out = {0};
    out.f64 = f64_sin(args[0].f64 + 1.5707963267948966);
    return out;
}

/* ---------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */

static VMReg n_clock_ns(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.i64 = (vm_i64)vm_sys_clock_ns();
    return out;
}

static VMReg n_unix_time(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.i64 = vm_sys_unix_time();
    return out;
}

/* ---------------------------------------------------------------------------
 * Files: each call sets the error text read by fs_error() ("" = success)
 * ------------------------------------------------------------------------- */

static const char *g_fs_error = "";

static VMReg n_fs_error(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.str_id = vm_chunk_add_string(chunk, g_fs_error, vm_str_len(g_fs_error));
    return out;
}

static VMReg n_fs_read(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    char sb[256];
    vm_u32 plen = 0;
    char *path = copy_cstr(chunk, args[0].str_id, sb, sizeof(sb), &plen);
    vm_u32 len = 0;
    char *data = (path != VM_NULL) ? vm_sys_file_read(path, &len) : VM_NULL;
    free_cstr(path, sb, plen);
    if (data == VM_NULL) {
        g_fs_error = "cannot read file";
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    g_fs_error = "";
    out.str_id = vm_chunk_new_string(chunk, data, len);
    vm_sys_file_free(data, len);
    return out;
}

static VMReg fs_write_impl(VMChunk *chunk, const VMReg *args, vm_bool append) {
    VMReg out = {0};
    char sb[256];
    vm_u32 plen = 0;
    char *path = copy_cstr(chunk, args[0].str_id, sb, sizeof(sb), &plen);
    vm_bool ok = VM_FALSE;
    if (path != VM_NULL) {
        VMString d = chunk->strings[args[1].str_id];
        ok = vm_sys_file_write(path, d.chars, d.length, append);
    }
    free_cstr(path, sb, plen);
    g_fs_error = ok ? "" : "cannot write file";
    out.b = ok;
    return out;
}

static VMReg n_fs_write(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    return fs_write_impl(chunk, args, VM_FALSE);
}

static VMReg n_fs_append(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    return fs_write_impl(chunk, args, VM_TRUE);
}

static VMReg n_fs_exists(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    char sb[256];
    vm_u32 plen = 0;
    char *path = copy_cstr(chunk, args[0].str_id, sb, sizeof(sb), &plen);
    out.b = (path != VM_NULL) ? vm_sys_file_exists(path) : VM_FALSE;
    free_cstr(path, sb, plen);
    return out;
}

static VMReg n_fs_remove(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    char sb[256];
    vm_u32 plen = 0;
    char *path = copy_cstr(chunk, args[0].str_id, sb, sizeof(sb), &plen);
    out.b = (path != VM_NULL) ? vm_sys_file_remove(path) : VM_FALSE;
    free_cstr(path, sb, plen);
    g_fs_error = out.b ? "" : "cannot remove file";
    return out;
}

/* ---------------------------------------------------------------------------
 * Environment (read-only OS variables) and standard input
 * ------------------------------------------------------------------------- */

static VMReg n_env_get(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    char sb[256];
    vm_u32 kl = 0;
    char *key = copy_cstr(chunk, args[0].str_id, sb, sizeof(sb), &kl);
    const char *v = (key != VM_NULL) ? vm_sys_getenv(key) : "";
    free_cstr(key, sb, kl);
    out.str_id = vm_chunk_new_string(chunk, v, vm_str_len(v));
    return out;
}

extern char *getenv(const char *name);

static VMReg n_env_has(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    char sb[256];
    vm_u32 kl = 0;
    char *key = copy_cstr(chunk, args[0].str_id, sb, sizeof(sb), &kl);
    out.b = vm_bool_from_int(key != VM_NULL && getenv(key) != VM_NULL);
    free_cstr(key, sb, kl);
    return out;
}

extern long read(int fd, void *buf, unsigned long count);
static vm_bool g_stdin_eof = VM_FALSE;

/* One line from stdin without the newline; sets the eof flag when nothing was read */
static VMReg n_io_read_line(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    char buf[4096];
    vm_u32 n = 0;
    vm_bool got = VM_FALSE;
    for (;;) {
        char ch;
        long r = read(0, &ch, 1);
        if (r <= 0) break;
        got = VM_TRUE;
        if (ch == '\n') break;
        if (n + 1 < sizeof(buf)) buf[n++] = ch;
    }
    if (n > 0 && buf[n - 1] == '\r') n--;
    g_stdin_eof = vm_bool_from_int(!got);
    out.str_id = vm_chunk_new_string(chunk, buf, n);
    return out;
}

static VMReg n_io_eof(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)chunk; (void)args; (void)argc;
    VMReg out = {0};
    out.b = g_stdin_eof;
    return out;
}

static VMReg n_io_write(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    VMString s = chunk->strings[args[0].str_id];
    vm_sys_write(s.chars, s.length);
    return out;
}

/* ---------------------------------------------------------------------------
 * Program arguments and exit
 * ------------------------------------------------------------------------- */

static VMReg n_os_args(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)args; (void)argc;
    VMReg out = {0};
    out.arr_id = vm_chunk_array_new(chunk, VM_TYPE_STRING);
    for (vm_u32 i = 0; i < chunk->prog_argc; i++) {
        VMReg s = {0};
        s.str_id = vm_chunk_new_string(chunk, chunk->prog_args[i], vm_str_len(chunk->prog_args[i]));
        vm_chunk_array_push(chunk, out.arr_id, s);
    }
    return out;
}

/* Stops the whole program with this exit code (the host decides what an exit code means) */
static VMReg n_os_exit(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    chunk->exit_requested = VM_TRUE;
    chunk->exit_code = (vm_i32)args[0].i64;
    chunk->native_error = VM_TRUE;   /* the CPU checks exit_requested on this (already taken) slow path */
    return out;
}

/* ---------------------------------------------------------------------------
 * Bytes: strings <-> arrays of byte values (0..255)
 * ------------------------------------------------------------------------- */

static VMReg n_str_to_bytes(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    out.arr_id = vm_chunk_array_new(chunk, VM_TYPE_I32);
    vm_u32 len = chunk->strings[args[0].str_id].length;
    for (vm_u32 i = 0; i < len; i++) {
        VMReg b = {0};
        b.i32 = (vm_i32)(unsigned char)chunk->strings[args[0].str_id].chars[i];   /* re-read: push may move memory */
        vm_chunk_array_push(chunk, out.arr_id, b);
    }
    return out;
}

static VMReg n_bytes_to_str(VMChunk *chunk, const VMReg *args, vm_u8 argc) {
    (void)argc;
    VMReg out = {0};
    if (args[0].arr_id >= chunk->num_arrays) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    vm_u32 len = chunk->arrays[args[0].arr_id].length;
    char *dst = vm_chunk_arena_reserve(chunk, len);
    if (dst == VM_NULL) {
        out.str_id = vm_chunk_add_string(chunk, "", 0);
        return out;
    }
    const VMArray *a = &chunk->arrays[args[0].arr_id];
    for (vm_u32 i = 0; i < len; i++) dst[i] = (char)(a->items[i].i32 & 0xFF);
    dst[len] = '\0';
    out.str_id = vm_chunk_commit_string(chunk, dst, len);
    return out;
}

/* ---------------------------------------------------------------------------
 * Table
 * ------------------------------------------------------------------------- */

#define OP1(name, arity, i0)         {name, arity, 1, {i0, 0, 0}, VM_FALSE, VM_NULL}
#define OPK(name, arity, i0)         {name, arity, 1, {i0, 0, 0}, VM_TRUE, VM_NULL}
#define OP2(name, arity, i0, i1)     {name, arity, 2, {i0, i1, 0}, VM_FALSE, VM_NULL}
#define NAT(name, arity, fn)         {name, arity, 0, {0, 0, 0}, VM_FALSE, fn}

static const VMIntrinsic g_intrinsics[] = {
    /* strings / bytes */
    OP1("str_len",       1, VM_MAKE_ABC(OP_STRLEN, 0, 0, 0)),
    OP1("str_upper",     1, VM_MAKE_ABC(OP_STRUPPER, 0, 0, 0)),
    OP1("str_lower",     1, VM_MAKE_ABC(OP_STRLOWER, 0, 0, 0)),
    OP1("str_trim",      1, VM_MAKE_ABC(OP_STRTRIM, 0, 0, 0)),
    OP1("str_sub",       3, VM_MAKE_ABC(OP_STRSUB, 0, 0, 1)),
    OP1("str_find",      2, VM_MAKE_ABC(OP_STRFIND, 0, 0, 1)),
    OP1("str_replace",   3, VM_MAKE_ABC(OP_STRREPLACE, 0, 0, 1)),
    NAT("str_byte",      2, n_str_byte),
    NAT("str_char",      1, n_str_char),
    NAT("str_find_from", 3, n_str_find_from),
    NAT("str_repeat",    2, n_str_repeat),
    NAT("str_hash",      1, n_str_hash),
    NAT("str_cmp",       2, n_str_cmp),
    NAT("parse_f64",     1, n_parse_f64),
    NAT("str_skip_ws",   2, n_str_skip_ws),
    NAT("str_scan_plain", 2, n_str_scan_plain),
    NAT("str_span_num",  2, n_str_span_num),
    /* arrays */
    OP1("arr_len",       1, VM_MAKE_ABC(OP_ARR_LEN, 0, 0, 0)),
    OP2("arr_push",      2, VM_MAKE_ABC(OP_ARR_PUSH, 0, 1, 0), VM_MAKE_ABC(OP_ARR_LEN, 0, 0, 0)),
    OP1("arr_pop",       1, VM_MAKE_ABC(OP_ARR_POP, 0, 0, 0)),
    OP1("arr_get",       2, VM_MAKE_ABC(OP_ARR_GET, 0, 0, 1)),
    OP1("arr_set",       3, VM_MAKE_ABC(OP_ARR_SET, 0, 1, 2)),
    NAT("arr_clear",     1, n_arr_clear),
    NAT("arr_join",      2, n_arr_join),
    /* math */
    OPK("sqrt32",        1, VM_MAKE_ABC(OP_MATH, 0, 0, 0)),
    OPK("floor32",       1, VM_MAKE_ABC(OP_MATH, 0, 0, 1)),
    OPK("ceil32",        1, VM_MAKE_ABC(OP_MATH, 0, 0, 2)),
    OPK("sin32",         1, VM_MAKE_ABC(OP_MATH, 0, 0, 3)),
    OPK("cos32",         1, VM_MAKE_ABC(OP_MATH, 0, 0, 4)),
    OPK("popcount32",    1, VM_MAKE_ABC(OP_MATH, 0, 0, 7)),
    NAT("sqrt64",        1, n_sqrt64),
    NAT("floor64",       1, n_floor64),
    NAT("ceil64",        1, n_ceil64),
    NAT("sin64",         1, n_sin64),
    NAT("cos64",         1, n_cos64),
    /* time */
    NAT("clock_ns",      0, n_clock_ns),
    NAT("unix_time",     0, n_unix_time),
    /* files */
    NAT("fs_read",       1, n_fs_read),
    NAT("fs_write",      2, n_fs_write),
    NAT("fs_append",     2, n_fs_append),
    NAT("fs_exists",     1, n_fs_exists),
    NAT("fs_remove",     1, n_fs_remove),
    NAT("fs_error",      0, n_fs_error),
    /* maps */
    OP1("map_len",       1, VM_MAKE_ABC(OP_MAP_LEN, 0, 0, 0)),
    OP1("map_has",       2, VM_MAKE_ABC(OP_MAP_HAS, 0, 0, 1)),
    OP1("map_get",       2, VM_MAKE_ABC(OP_MAP_GET, 0, 0, 1)),
    OP1("map_set",       3, VM_MAKE_ABC(OP_MAP_SET, 0, 1, 2)),
    OP1("map_remove",    2, VM_MAKE_ABC(OP_MAP_DEL, 0, 0, 1)),
    OP1("map_key_at",    2, VM_MAKE_ABC(OP_MAP_KEY_AT, 0, 0, 1)),
    NAT("map_clear",     1, native_map_clear),
    NAT("map_keys",      1, native_map_keys),
    /* console */
    OP1("print_str",     1, VM_MAKE_ABC(OP_PRINT_STR, 0, 0, 0)),
    OP1("print_i32",     1, VM_MAKE_ABC(OP_PRINT_I32, 0, 0, 0)),
    OP1("print_i64",     1, VM_MAKE_ABC(OP_PRINT_I64, 0, 0, 0)),
    OP1("print_f32",     1, VM_MAKE_ABC(OP_PRINT_F32, 0, 0, 0)),
    OP1("print_f64",     1, VM_MAKE_ABC(OP_PRINT_F64, 0, 0, 0)),
    OP1("print_bool",    1, VM_MAKE_ABC(OP_PRINT_BOOL, 0, 0, 0)),
    NAT("io_write",      1, n_io_write),
    NAT("io_read_line",  0, n_io_read_line),
    NAT("io_eof",        0, n_io_eof),
    /* environment / platform */
    NAT("env_get",       1, n_env_get),
    NAT("env_has",       1, n_env_has),
    NAT("os_platform",   0, native_os_platform),
    NAT("os_arch",       0, native_os_arch),
    NAT("os_args",       0, n_os_args),
    NAT("os_exit",       1, n_os_exit),
    /* bytes */
    NAT("str_to_bytes",  1, n_str_to_bytes),
    NAT("bytes_to_str",  1, n_bytes_to_str),
    /* tasks (scheduler lives in the VM) */
    NAT("task_spawn",    2, native_task_spawn),
    NAT("task_defer",    2, native_task_defer),
    NAT("task_delay",    3, native_task_delay),
    NAT("task_cancel",   1, native_task_cancel),
    NAT("task_step",     0, native_task_step),
    NAT("task_status",   1, native_task_status),
    NAT("task_result",   1, native_task_result),
    NAT("task_error",    1, native_task_error),
    NAT("task_ok",       1, native_task_ok),
    /* VM introspection */
    NAT("vm_gc",            0, native_dbg_gc),
    NAT("vm_gc_count",      0, native_dbg_gc_count),
    NAT("vm_live_arrays",   0, native_dbg_live_arrays),
    NAT("vm_live_strings",  0, native_dbg_live_strings),
    NAT("vm_func_count",    0, native_dbg_func_count),
    NAT("vm_bytecode_size", 0, native_dbg_bytecode_size),
    NAT("vm_string_count",  0, native_dbg_string_count),
    NAT("vm_arena_used",    0, native_dbg_arena_used),
    NAT("vm_array_count",   0, native_dbg_array_count),
    NAT("vm_map_count",     0, native_dbg_map_count),
    NAT("vm_func_name",     1, native_dbg_func_name),
    NAT("vm_traceback",     0, native_dbg_traceback),
    NAT("vm_last_error",    0, native_dbg_last_error),
};

#define NUM_INTRINSICS (sizeof(g_intrinsics) / sizeof(g_intrinsics[0]))

const VMIntrinsic *vm_intrinsic_find(const char *name, vm_u32 len) {
    for (vm_u32 i = 0; i < NUM_INTRINSICS; i++) {
        if (vm_str_eq_cstr(name, len, g_intrinsics[i].name) == VM_TRUE) return &g_intrinsics[i];
    }
    return VM_NULL;
}

vm_u32 vm_intrinsic_count(void) {
    return (vm_u32)NUM_INTRINSICS;
}

const VMIntrinsic *vm_intrinsic_at(vm_u32 i) {
    return (i < NUM_INTRINSICS) ? &g_intrinsics[i] : VM_NULL;
}

void vm_intrinsics_register_natives(VMChunk *chunk) {
    for (vm_u32 i = 0; i < NUM_INTRINSICS; i++) {
        const VMIntrinsic *it = &g_intrinsics[i];
        if (it->num_ops != 0) continue;
        char name[64];
        name[0] = '$';
        vm_u32 n = vm_str_len(it->name);
        vm_str_copy(name + 1, it->name, n, 63);
        (void)vm_lib_add_native(chunk, name, it->arity, VM_NULL, VM_TYPE_I32, it->native);
    }
}
