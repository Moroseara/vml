#include "vm_syscall.h"
#include "../string/vm_string.h"

extern long write(int fd, const void *buf, unsigned long count);

#define VM_STDOUT_FD       1
#define VM_OUTPUT_BUF_CAP  65536

static VMOutputCallback g_output_cb = VM_NULL;
static vm_bool          g_capture_enabled = VM_FALSE;
static char             g_output_buf[VM_OUTPUT_BUF_CAP];
static vm_u32           g_output_len = 0;
static vm_bool          g_output_silent = VM_FALSE;

void vm_sys_set_output_silent(vm_bool silent) {
    g_output_silent = silent;
}

void vm_sys_set_output_callback(VMOutputCallback cb) {
    g_output_cb = cb;
}

void vm_sys_enable_output_buffer(vm_bool enable) {
    g_capture_enabled = enable;
}

const char *vm_sys_get_output_buffer(vm_u32 *out_len) {
    if (out_len != VM_NULL) *out_len = g_output_len;
    g_output_buf[g_output_len] = '\0';
    return g_output_buf;
}

void vm_sys_clear_output_buffer(void) {
    g_output_len = 0;
    g_output_buf[0] = '\0';
}

void vm_sys_write(const char *buf, vm_size len) {
    if (len == 0) return;
    if (g_capture_enabled == VM_TRUE) {
        for (vm_size i = 0; i < len && g_output_len + 1 < VM_OUTPUT_BUF_CAP; i++) {
            g_output_buf[g_output_len++] = buf[i];
        }
        g_output_buf[g_output_len] = '\0';
        if (g_output_silent == VM_TRUE) return;   /* capture only (compile errors for vml_last_error) */
    }
    if (g_output_cb != VM_NULL) {
        g_output_cb(buf, len);
        return;
    }
    (void)write(VM_STDOUT_FD, buf, len);
}

void vm_sys_write_cstr(const char *str) {
    vm_sys_write(str, (vm_size)vm_str_len(str));
}

void vm_sys_print_i64(vm_i64 val) {
    char buf[32];
    int i = 30;
    buf[31] = '\n';
    vm_u64 uval;
    vm_bool neg = VM_FALSE;

    if (val < 0) {
        neg = VM_TRUE;
        uval = (vm_u64)(-(val + 1)) + 1u;
    } else {
        uval = (vm_u64)val;
    }

    if (uval == 0) {
        buf[i--] = '0';
    } else {
        while (uval > 0) {
            buf[i--] = (char)('0' + (uval % 10u));
            uval /= 10u;
        }
    }
    if (neg == VM_TRUE) {
        buf[i--] = '-';
    }
    vm_sys_write(&buf[i + 1], (vm_size)(31 - i));
}

void vm_sys_print_i32(vm_i32 val) {
    vm_sys_print_i64((vm_i64)val);
}

void vm_sys_print_f64(vm_f64 val) {
    char buf[40];
    vm_u32 n = vm_f64_to_str(buf, val, VM_FALSE);
    buf[n++] = '\n';
    vm_sys_write(buf, n);
}

void vm_sys_print_f32(vm_f32 val) {
    char buf[40];
    vm_u32 n = vm_f64_to_str(buf, (vm_f64)val, VM_TRUE);
    buf[n++] = '\n';
    vm_sys_write(buf, n);
}

void vm_sys_print_bool(vm_bool val) {
    if (val == VM_TRUE) {
        vm_sys_write("true\n", 5);
    } else {
        vm_sys_write("false\n", 6);
    }
}

void vm_sys_print_str(const char *chars, vm_u32 len) {
    vm_sys_write(chars, (vm_size)len);
    vm_sys_write("\n", 1);
}

struct vm_timespec {
    long tv_sec;
    long tv_nsec;
};
extern int clock_gettime(int clk_id, struct vm_timespec *tp);
extern char *getenv(const char *name);

vm_u64 vm_sys_clock_ns(void) {
    struct vm_timespec ts = {0, 0};
    (void)clock_gettime(6, &ts);
    if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
        (void)clock_gettime(0, &ts);
    }
    return (vm_u64)ts.tv_sec * 1000000000ULL + (vm_u64)ts.tv_nsec;
}

vm_i64 vm_sys_unix_time(void) {
    struct vm_timespec ts = {0, 0};
    (void)clock_gettime(0, &ts);
    return (vm_i64)ts.tv_sec;
}

const char *vm_sys_getenv(const char *key) {
    if (key == VM_NULL) return "";
    const char *v = getenv(key);
    return (v != VM_NULL) ? v : "";
}

const char *vm_sys_platform(void) {
#if defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#else
    return "posix";
#endif
}

const char *vm_sys_arch(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#else
    return "generic";
#endif
}

extern void *realloc(void *ptr, unsigned long size);
extern void  free(void *ptr);

void *vm_sys_default_realloc(void *user_data, void *ptr, vm_size old_size, vm_size new_size) {
    (void)user_data;
    (void)old_size;
    if (new_size == 0) {
        free(ptr);
        return VM_NULL;
    }
    return realloc(ptr, new_size);
}

/* ---------------------------------------------------------------------------
 * Files
 * ------------------------------------------------------------------------- */

extern int  open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long count);
extern int  close(int fd);
extern int  unlink(const char *path);
extern int  access(const char *path, int mode);

#if defined(__APPLE__)
#define VM_O_WRONLY 0x0001
#define VM_O_APPEND 0x0008
#define VM_O_CREAT  0x0200
#define VM_O_TRUNC  0x0400
#else
#define VM_O_WRONLY 0x0001
#define VM_O_APPEND 0x0400
#define VM_O_CREAT  0x0040
#define VM_O_TRUNC  0x0200
#endif

char *vm_sys_file_read(const char *path, vm_u32 *out_len) {
    *out_len = 0;
    int fd = open(path, 0);
    if (fd < 0) return VM_NULL;
    vm_u32 cap = 4096, len = 0;
    char *buf = (char *)vm_sys_default_realloc(VM_NULL, VM_NULL, 0, cap);
    if (buf == VM_NULL) {
        close(fd);
        return VM_NULL;
    }
    for (;;) {
        if (len == cap) {
            char *nb = (char *)vm_sys_default_realloc(VM_NULL, buf, cap, (vm_size)cap * 2);
            if (nb == VM_NULL) {
                vm_sys_default_realloc(VM_NULL, buf, cap, 0);
                close(fd);
                return VM_NULL;
            }
            buf = nb;
            cap *= 2;
        }
        long n = read(fd, buf + len, cap - len);
        if (n < 0) {
            vm_sys_default_realloc(VM_NULL, buf, cap, 0);
            close(fd);
            return VM_NULL;
        }
        if (n == 0) break;
        len += (vm_u32)n;
    }
    close(fd);
    *out_len = len;
    return buf;
}

void vm_sys_file_free(char *buf, vm_u32 len) {
    if (buf != VM_NULL) vm_sys_default_realloc(VM_NULL, buf, len, 0);
}

vm_bool vm_sys_file_write(const char *path, const char *data, vm_u32 len, vm_bool append) {
    int flags = VM_O_WRONLY | VM_O_CREAT | (append ? VM_O_APPEND : VM_O_TRUNC);
    int fd = open(path, flags, 0644);
    if (fd < 0) return VM_FALSE;
    vm_u32 done = 0;
    while (done < len) {
        long n = write(fd, data + done, len - done);
        if (n <= 0) {
            close(fd);
            return VM_FALSE;
        }
        done += (vm_u32)n;
    }
    close(fd);
    return VM_TRUE;
}

vm_bool vm_sys_file_exists(const char *path) {
    return vm_bool_from_int(access(path, 0) == 0);
}

vm_bool vm_sys_file_remove(const char *path) {
    return vm_bool_from_int(unlink(path) == 0);
}
