/*
 * Self-contained programs: `vml build` writes a copy of this vml binary with the program's
 * .vmb image appended, followed by a 16-byte trailer:
 *
 *   [vml binary][image bytes][u64 image length, little endian]["VMLEXE01"]
 *
 * At startup vml looks at the end of its own file. If the trailer is there it is a built
 * program: it runs the image, and every argument belongs to the program (os.args()).
 */
#include "cli.h"
#include "../../include/vml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define EXE_MAGIC "VMLEXE01"

int cli_self_path(char *out, size_t cap, const char *argv0) {
    out[0] = '\0';
#if defined(__APPLE__)
    uint32_t size = (uint32_t)cap;
    if (_NSGetExecutablePath(out, &size) != 0) out[0] = '\0';
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    out[n > 0 ? n : 0] = '\0';
#endif
    if (out[0] == '\0' && argv0 != NULL) snprintf(out, cap, "%s", argv0);
    return out[0] != '\0';
}

/* Size of the plain vml binary inside `f` (without an appended program); *image_len = 0 if none */
static long runtime_size(FILE *f, uint64_t *image_len) {
    *image_len = 0;
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long total = ftell(f);
    if (total < 16) return total;
    unsigned char tail[16];
    if (fseek(f, total - 16, SEEK_SET) != 0 || fread(tail, 1, 16, f) != 16) return total;
    if (memcmp(tail + 8, EXE_MAGIC, 8) != 0) return total;
    uint64_t len = 0;
    for (int i = 7; i >= 0; i--) len = (len << 8) | tail[i];
    if (len == 0 || len > (uint64_t)(total - 16)) return total;
    *image_len = len;
    return total - 16 - (long)len;
}

int cli_run_embedded(int argc, char **argv) {
    char self[1024];
    if (!cli_self_path(self, sizeof(self), argv[0])) return -1;
    FILE *f = fopen(self, "rb");
    if (f == NULL) return -1;
    uint64_t len = 0;
    long start = runtime_size(f, &len);
    if (len == 0 || start < 0) {
        fclose(f);
        return -1;
    }
    unsigned char *image = (unsigned char *)malloc((size_t)len);
    int ok = image != NULL && fseek(f, start, SEEK_SET) == 0 && fread(image, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    if (!ok) {
        fprintf(stderr, "%s: cannot read the program stored in this file\n", argv[0]);
        free(image);
        return 1;
    }
    VML_State *L = vml_new(NULL);
    vml_set_args(L, argc - 1, (const char *const *)(argv + 1));
    VML_Result r = vml_load_image(L, image, (size_t)len);
    if (r == VML_OK) r = vml_run_main(L);
    int code = 0;
    if (r == VML_OK && vml_exit_requested(L, &code)) {
        /* os.exit(code) */
    } else if (r != VML_OK) {
        fprintf(stderr, "[Runtime Error] %s\n", vml_last_error(L));
        code = 1;
    }
    vml_free(L);
    free(image);
    return code;
}

int cli_write_executable(const char *image_path, const char *out_path, const char *argv0) {
    char self[1024];
    if (!cli_self_path(self, sizeof(self), argv0)) {
        fprintf(stderr, "vml: cannot find the vml binary to build from\n");
        return 0;
    }
    FILE *in = fopen(self, "rb");
    FILE *img = fopen(image_path, "rb");
    if (in == NULL || img == NULL) {
        if (in) fclose(in);
        if (img) fclose(img);
        fprintf(stderr, "vml: cannot read %s\n", in == NULL ? self : image_path);
        return 0;
    }
    uint64_t old_len = 0;
    long rt = runtime_size(in, &old_len);   /* building from a built program keeps only the runtime */
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", out_path);
    FILE *out = fopen(tmp, "wb");
    if (out == NULL || rt < 0) {
        fclose(in);
        fclose(img);
        if (out) fclose(out);
        fprintf(stderr, "vml: cannot write %s\n", out_path);
        return 0;
    }
    char buf[65536];
    fseek(in, 0, SEEK_SET);
    long left = rt;
    int ok = 1;
    while (left > 0 && ok) {
        size_t want = left < (long)sizeof(buf) ? (size_t)left : sizeof(buf);
        size_t got = fread(buf, 1, want, in);
        ok = got == want && fwrite(buf, 1, got, out) == got;
        left -= (long)got;
    }
    uint64_t ilen = 0;
    size_t got;
    while (ok && (got = fread(buf, 1, sizeof(buf), img)) > 0) {
        ok = fwrite(buf, 1, got, out) == got;
        ilen += got;
    }
    unsigned char tail[16];
    for (int i = 0; i < 8; i++) tail[i] = (unsigned char)((ilen >> (8 * i)) & 0xFF);
    memcpy(tail + 8, EXE_MAGIC, 8);
    ok = ok && ilen > 0 && fwrite(tail, 1, 16, out) == 16;
    fclose(in);
    fclose(img);
    ok = (fclose(out) == 0) && ok;
    if (!ok || chmod(tmp, 0755) != 0 || rename(tmp, out_path) != 0) {
        remove(tmp);
        fprintf(stderr, "vml: cannot write %s\n", out_path);
        return 0;
    }
    return 1;
}
