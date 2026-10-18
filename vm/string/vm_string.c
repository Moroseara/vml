#include "vm_string.h"

vm_u32 vm_str_len(const char *s) {
    vm_u32 len = 0;
    if (s == VM_NULL) return 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

vm_bool vm_str_eq(const char *a, vm_u32 alen, const char *b, vm_u32 blen) {
    if (alen != blen) return VM_FALSE;
    for (vm_u32 i = 0; i < alen; i++) {
        if (a[i] != b[i]) return VM_FALSE;
    }
    return VM_TRUE;
}

vm_bool vm_str_eq_cstr(const char *a, vm_u32 alen, const char *cstr) {
    return vm_str_eq(a, alen, cstr, vm_str_len(cstr));
}

void vm_str_copy(char *dst, const char *src, vm_u32 len, vm_u32 max_len) {
    if (max_len == 0) return;
    if (len >= max_len) {
        len = max_len - 1;
    }
    for (vm_u32 i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
}

void vm_str_join_dot(char *dst, vm_u32 max_len,
                     const char *prefix, vm_u32 plen,
                     const char *name, vm_u32 nlen) {
    vm_u32 pos = 0;
    vm_u32 half = (max_len > 4) ? (max_len / 2) : max_len;
    for (vm_u32 i = 0; i < plen && pos + 2 < half; i++) {
        dst[pos++] = prefix[i];
    }
    if (plen > 0 && prefix[plen - 1] != '.' && pos + 2 < max_len) {
        dst[pos++] = '.';
    }
    for (vm_u32 i = 0; i < nlen && pos + 1 < max_len; i++) {
        dst[pos++] = name[i];
    }
    dst[pos] = '\0';
}

vm_i64 vm_str_parse_i64(const char *s, vm_u32 len) {
    vm_u32 i = 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    vm_i64 sign = 1;
    if (i < len && (s[i] == '-' || s[i] == '+')) {
        if (s[i] == '-') sign = -1;
        i++;
    }
    vm_i64 val = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        val = val * 10 + (s[i] - '0');
        i++;
    }
    return val * sign;
}

extern double strtod(const char *nptr, char **endptr);

/* Exact (correctly rounded) decimal -> double; accepts sign, fraction, exponent and '_' separators */
vm_f64 vm_str_parse_f64(const char *s, vm_u32 len) {
    char buf[128];
    vm_u32 n = 0;
    for (vm_u32 i = 0; i < len && n + 1 < sizeof(buf); i++) {
        if (s[i] != '_') buf[n++] = s[i];
    }
    buf[n] = '\0';
    return strtod(buf, VM_NULL);
}

vm_u32 vm_i64_to_str(char *dst, vm_i64 val) {
    char tmp[24];
    int t = 0;
    vm_u32 pos = 0;
    if (val < 0) {
        dst[pos++] = '-';
        val = -val;
    }
    if (val == 0) {
        dst[pos++] = '0';
        dst[pos] = '\0';
        return pos;
    }
    while (val > 0) {
        tmp[t++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (t > 0) {
        dst[pos++] = tmp[--t];
    }
    dst[pos] = '\0';
    return pos;
}

extern int    snprintf(char *str, unsigned long size, const char *format, ...);

vm_u32 vm_f64_to_str(char *out, vm_f64 v, vm_bool is_f32) {
    if (v != v) { vm_str_copy(out, "nan", 3, 32); return 3; }
    if (v > 1.7976931348623157e308) { vm_str_copy(out, "inf", 3, 32); return 3; }
    if (v < -1.7976931348623157e308) { vm_str_copy(out, "-inf", 4, 32); return 4; }
    /* Whole numbers that fit exactly: plain digits */
    if (v == (vm_f64)(vm_i64)v && v > -1e15 && v < 1e15) {
        if (v == 0.0 && 1.0 / v < 0.0) { vm_str_copy(out, "-0", 2, 32); return 2; }
        return vm_i64_to_str(out, (vm_i64)v);
    }
    int max_p = is_f32 ? 9 : 17;
    int n = 0;
    for (int p = 1; p <= max_p; p++) {
        n = snprintf(out, 32, "%.*g", p, v);
        vm_f64 back = strtod(out, VM_NULL);
        if (is_f32 ? ((vm_f32)back == (vm_f32)v) : (back == v)) break;
    }
    return (vm_u32)n;
}
