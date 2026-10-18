/*
 * Projects (SPEC 6b): vml.toml, path dependencies, and the build cache.
 *
 *   vml new <name>   create a project folder
 *   vml run [-v]     build if anything changed (else reuse .vml-cache), then run
 *   vml build [-o f] build into .vml-cache, then write the program dist/<name> (or -o; *.vmb = image)
 *   vml test         run every .vml file in tests/ with the project's packages mounted
 *   vml clean        remove .vml-cache
 *
 * The project's own modules live in src/ and are imported as `<name>.<module>`
 * (`import game.ui` -> src/ui.vml). Each path dependency is mounted the same way
 * under its own name. The cache key hashes the vml binary, every vml.toml and every
 * .vml file under each src/ folder, so any change rebuilds and nothing stale runs.
 */
#include "cli.h"
#include "../../include/vml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define MAX_MOUNTS 8
#define PATH_CAP   512

/* ---------------------------------------------------------------------------
 * vml.toml: the subset a manifest needs — [sections], key = value, where value is
 * "string", ["a", "b"], { k = "v", ... }, a number or true/false; '#' comments.
 * ------------------------------------------------------------------------- */

typedef struct {
    char section[64];
    char key[64];
    char value[512];
    int  line;
} TomlEntry;

typedef struct {
    TomlEntry *items;
    int        count, cap;
    char       error[300];
} Toml;

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    return s;
}

/* Cuts a '#' comment that is not inside a string */
static void strip_comment(char *s) {
    int in_str = 0;
    for (char *p = s; *p; p++) {
        if (*p == '"' && (p == s || p[-1] != '\\')) in_str = !in_str;
        else if (*p == '#' && !in_str) { *p = '\0'; return; }
    }
}

static int is_key_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
}

static int toml_parse(Toml *t, const char *text, const char *file) {
    memset(t, 0, sizeof(*t));
    char section[64] = "";
    int line_no = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char buf[1024];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = '\0';
        p = eol ? eol + 1 : p + len;
        line_no++;
        strip_comment(buf);
        char *s = trim(buf);
        if (*s == '\0') continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (close == NULL) {
                snprintf(t->error, sizeof(t->error), "%s:%d: expected ']' after section name", file, line_no);
                return 0;
            }
            *close = '\0';
            snprintf(section, sizeof(section), "%s", trim(s + 1));
            continue;
        }
        char *eq = strchr(s, '=');
        if (eq == NULL) {
            snprintf(t->error, sizeof(t->error), "%s:%d: expected 'key = value'", file, line_no);
            return 0;
        }
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);
        for (char *k = key; *k; k++) {
            if (!is_key_char(*k)) {
                snprintf(t->error, sizeof(t->error), "%s:%d: invalid key '%s'", file, line_no, key);
                return 0;
            }
        }
        if (*key == '\0' || *val == '\0') {
            snprintf(t->error, sizeof(t->error), "%s:%d: expected 'key = value'", file, line_no);
            return 0;
        }
        char first = *val;
        size_t vl = strlen(val);
        int ok = (first == '"' && vl >= 2 && val[vl - 1] == '"') || (first == '[' && val[vl - 1] == ']') ||
                 (first == '{' && val[vl - 1] == '}') || (first >= '0' && first <= '9') || first == '-' ||
                 strcmp(val, "true") == 0 || strcmp(val, "false") == 0;
        if (!ok) {
            snprintf(t->error, sizeof(t->error), "%s:%d: value of '%s' must be a \"string\", [list], { table }, number or true/false",
                     file, line_no, key);
            return 0;
        }
        if (t->count == t->cap) {
            t->cap = t->cap ? t->cap * 2 : 16;
            t->items = (TomlEntry *)realloc(t->items, sizeof(TomlEntry) * (size_t)t->cap);
        }
        TomlEntry *e = &t->items[t->count++];
        snprintf(e->section, sizeof(e->section), "%s", section);
        snprintf(e->key, sizeof(e->key), "%s", key);
        snprintf(e->value, sizeof(e->value), "%s", val);
        e->line = line_no;
    }
    return 1;
}

static void toml_free(Toml *t) {
    free(t->items);
    t->items = NULL;
    t->count = t->cap = 0;
}

static const char *toml_get(const Toml *t, const char *section, const char *key) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].section, section) == 0 && strcmp(t->items[i].key, key) == 0) return t->items[i].value;
    }
    return NULL;
}

/* Reads a "string" starting at *p (which must point at the quote); advances *p past it */
static int read_quoted(const char **p, char *out, size_t cap) {
    const char *s = *p;
    if (*s != '"') return 0;
    s++;
    size_t n = 0;
    while (*s && *s != '"') {
        char ch = *s++;
        if (ch == '\\' && *s) {
            char e = *s++;
            ch = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
        }
        if (n + 1 < cap) out[n++] = ch;
    }
    if (*s != '"') return 0;
    out[n] = '\0';
    *p = s + 1;
    return 1;
}

static int toml_string(const char *raw, char *out, size_t cap) {
    if (raw == NULL) return 0;
    const char *p = raw;
    return read_quoted(&p, out, cap);
}

/* `{ path = "../x", as = "y" }` -> value of `key` */
static int toml_inline_get(const char *raw, const char *key, char *out, size_t cap) {
    if (raw == NULL || raw[0] != '{') return 0;
    const char *p = raw + 1;
    size_t kl = strlen(key);
    while (*p && *p != '}') {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        const char *ks = p;
        while (is_key_char(*p)) p++;
        size_t this_len = (size_t)(p - ks);
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') return 0;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char val[512];
        if (*p == '"') {
            if (!read_quoted(&p, val, sizeof(val))) return 0;
        } else {
            size_t n = 0;
            while (*p && *p != ',' && *p != '}' && n + 1 < sizeof(val)) val[n++] = *p++;
            val[n] = '\0';
            char *tv = trim(val);
            memmove(val, tv, strlen(tv) + 1);
        }
        if (this_len == kl && strncmp(ks, key, kl) == 0) {
            snprintf(out, cap, "%s", val);
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Files
 * ------------------------------------------------------------------------- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
        }
        size_t n = fread(buf + len, 1, cap - len - 1, f);
        if (n == 0) break;
        len += n;
    }
    fclose(f);
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static int write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static int copy_file(const char *from, const char *to) {
    size_t len = 0;
    char *data = read_file(from, &len);
    if (data == NULL) return 0;
    FILE *f = fopen(to, "wb");
    if (f == NULL) { free(data); return 0; }
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    free(data);
    return w == len;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void join_path(char *out, const char *a, const char *b) {
    if (a[0] == '\0' || strcmp(a, ".") == 0) snprintf(out, PATH_CAP, "%s", b);
    else snprintf(out, PATH_CAP, "%s/%s", a, b);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Sorted names in a folder (caller frees each and the array) */
static char **list_dir(const char *dir, int *count) {
    *count = 0;
    DIR *d = opendir(dir);
    if (d == NULL) return NULL;
    int cap = 16;
    char **names = (char **)malloc(sizeof(char *) * (size_t)cap);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (*count == cap) {
            cap *= 2;
            names = (char **)realloc(names, sizeof(char *) * (size_t)cap);
        }
        names[(*count)++] = strdup(ent->d_name);
    }
    closedir(d);
    qsort(names, (size_t)*count, sizeof(char *), cmp_str);
    return names;
}

static void free_list(char **names, int count) {
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

/* ---------------------------------------------------------------------------
 * Hash (FNV-1a 64) for the cache key
 * ------------------------------------------------------------------------- */

static void hash_bytes(uint64_t *h, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        *h ^= p[i];
        *h *= 1099511628211ULL;
    }
}

static void hash_str(uint64_t *h, const char *s) {
    hash_bytes(h, s, strlen(s) + 1);
}

static void hash_file(uint64_t *h, const char *path) {
    size_t len = 0;
    char *data = read_file(path, &len);
    hash_str(h, path);
    if (data == NULL) {
        hash_str(h, "<missing>");
        return;
    }
    hash_bytes(h, data, len);
    free(data);
}

/* The running vml binary: a new compiler never reuses images built by an old one */
static void hash_self(uint64_t *h, const char *argv0) {
    char path[PATH_CAP];
    path[0] = '\0';
#if defined(__APPLE__)
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) path[0] = '\0';
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    path[n > 0 ? n : 0] = '\0';
#endif
    if (path[0] == '\0') snprintf(path, sizeof(path), "%s", argv0);
    size_t len = 0;
    char *data = read_file(path, &len);
    if (data == NULL) {
        hash_str(h, vml_version());
        return;
    }
    hash_bytes(h, data, len);
    free(data);
}

/* ---------------------------------------------------------------------------
 * Which files are source: include / exclude in vml.toml, then .vmlignore, then
 * .gitignore (respect_gitignore = true by default), then built-in defaults.
 * Patterns use .gitignore syntax: `name`, `dir/`, `/anchored`, `*.ext`, `**`, `!negate`.
 * The same rules drive the cache key and test discovery, so they never disagree.
 * ------------------------------------------------------------------------- */

typedef struct {
    char pat[200];
    int  neg, dir_only, anchored;
} IgnRule;

typedef struct {
    IgnRule rules[160];
    int     count;
} Ignore;

static void ign_add(Ignore *ig, const char *line, int force_neg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", line);
    char *s = trim(buf);
    if (*s == '\0' || *s == '#' || ig->count >= 160) return;
    IgnRule *r = &ig->rules[ig->count];
    memset(r, 0, sizeof(*r));
    if (*s == '!') { r->neg = 1; s++; }
    if (force_neg) r->neg = 1;
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '/') { r->dir_only = 1; s[--n] = '\0'; }
    if (*s == '/') { r->anchored = 1; s++; }
    if (strncmp(s, "**/", 3) == 0) s += 3;          /* leading double-star slash = at any depth */
    else if (strchr(s, '/') != NULL) r->anchored = 1; /* a slash inside = relative to the folder */
    if (*s == '\0') return;
    snprintf(r->pat, sizeof(r->pat), "%s", s);
    ig->count++;
}

static void ign_add_file(Ignore *ig, const char *path) {
    char *text = read_file(path, NULL);
    if (text == NULL) return;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) ign_add(ig, line, 0);
    free(text);
}

/* Glob with `*` (not across '/'), `?`, and `**` (across '/') */
static int glob_match(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (p[0] == '*' && p[1] == '*') {
            p += 2;
            if (*p == '/') p++;
            for (const char *t = s;; t++) {
                if (glob_match(p, t)) return 1;
                if (*t == '\0') return 0;
            }
        }
        if (*p == '*') {
            for (const char *t = s;; t++) {
                if (glob_match(p + 1, t)) return 1;
                if (*t == '\0' || *t == '/') return 0;
            }
        }
        if (*s == '\0') return 0;
        if (*p != '?' && *p != *s) return 0;
    }
    return *s == '\0';
}

/* rel: path inside the package ("a/b.vml"); the last matching rule decides */
static int ign_skip(const Ignore *ig, const char *rel, int is_directory) {
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    int skip = 0;
    for (int i = 0; i < ig->count; i++) {
        const IgnRule *r = &ig->rules[i];
        if (r->dir_only && !is_directory) continue;
        int hit = r->anchored ? glob_match(r->pat, rel) : glob_match(r->pat, base);
        if (hit) skip = !r->neg;
    }
    return skip;
}

/* ---------------------------------------------------------------------------
 * Project + dependencies + workspace
 * ------------------------------------------------------------------------- */

typedef struct {
    char    name[64];
    char    dir[PATH_CAP];      /* package folder (relative to the current project root) */
    char    src[PATH_CAP];      /* where its modules start (`source =`, else src/ if present, else dir) */
    int     builtin;            /* std: served by the binary, never mounted */
    Ignore *ign;                /* rules relative to src */
} Mount;

typedef struct {
    char  name[64];
    char  version[32];
    char  entry[PATH_CAP];
    Mount mounts[MAX_MOUNTS];   /* [0] = this project */
    int   num_mounts;
    char  error[600];
} Project;

static int valid_name(const char *s) {
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z') || s[0] == '_')) return 0;
    for (const char *p = s; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
    }
    return 1;
}

/* Walks up from the current folder to the one holding vml.toml and changes into it */
static int enter_project_root(void) {
    char cwd[PATH_CAP];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return 0;
    for (;;) {
        char probe[PATH_CAP + 16];
        snprintf(probe, sizeof(probe), "%s/vml.toml", cwd);
        if (file_exists(probe)) return chdir(cwd) == 0;
        char *slash = strrchr(cwd, '/');
        if (slash == NULL || slash == cwd) return 0;
        *slash = '\0';
    }
}

static void free_project(Project *pr) {
    for (int i = 0; i < pr->num_mounts; i++) free(pr->mounts[i].ign);
    pr->num_mounts = 0;
}

/* Reads a ["a", "b"] list into callback-sized items */
static int toml_list_item(const char *raw, int index, char *out, size_t cap) {
    if (raw == NULL || raw[0] != '[') return 0;
    const char *p = raw + 1;
    for (int i = 0;; i++) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (*p != '"') return 0;
        char item[512];
        if (!read_quoted(&p, item, sizeof(item))) return 0;
        if (i == index) {
            snprintf(out, cap, "%s", item);
            return 1;
        }
    }
}

static Ignore *build_ignore(const Toml *t, const char *src_dir, int source_is_root) {
    Ignore *ig = (Ignore *)calloc(1, sizeof(Ignore));
    ign_add(ig, ".*", 0);              /* .git, .vml-cache, editor folders */
    ign_add(ig, "/dist/", 0);
    if (source_is_root) ign_add(ig, "/tests/", 0);
    const char *rg = toml_get(t, "project", "respect_gitignore");
    char p[PATH_CAP];
    if (rg == NULL || strcmp(rg, "false") != 0) {
        join_path(p, src_dir, ".gitignore");
        ign_add_file(ig, p);
    }
    join_path(p, src_dir, ".vmlignore");
    ign_add_file(ig, p);
    char item[512];
    for (int i = 0; toml_list_item(toml_get(t, "project", "exclude"), i, item, sizeof(item)); i++) ign_add(ig, item, 0);
    for (int i = 0; toml_list_item(toml_get(t, "project", "include"), i, item, sizeof(item)); i++) ign_add(ig, item, 1);
    return ig;
}

static int add_mount(Project *pr, const char *name, const char *dir, const char *src, int builtin, Ignore *ign) {
    for (int i = 0; i < pr->num_mounts; i++) {
        if (strcmp(pr->mounts[i].name, name) != 0) continue;
        free(ign);
        if (strcmp(pr->mounts[i].dir, dir) == 0) return 1;   /* same package reached twice */
        snprintf(pr->error, sizeof(pr->error), "two different packages are both named '%s' (%s and %s)",
                 name, pr->mounts[i].dir, dir);
        return 0;
    }
    if (pr->num_mounts >= MAX_MOUNTS) {
        free(ign);
        snprintf(pr->error, sizeof(pr->error), "too many packages (limit %d)", MAX_MOUNTS);
        return 0;
    }
    Mount *m = &pr->mounts[pr->num_mounts++];
    snprintf(m->name, 64, "%s", name);
    snprintf(m->dir, PATH_CAP, "%s", dir);
    snprintf(m->src, PATH_CAP, "%s", src);
    m->builtin = builtin;
    m->ign = ign;
    return 1;
}

/* Folder of the nearest workspace above `dir` (as a relative path), or 0 */
static int find_workspace(const char *dir, char *out) {
    char probe[PATH_CAP], up[PATH_CAP];
    snprintf(up, sizeof(up), "%s", dir);
    for (int depth = 0; depth < 8; depth++) {
        char next[PATH_CAP];
        join_path(next, up, "..");
        snprintf(up, sizeof(up), "%s", next);
        join_path(probe, up, "vml.toml");
        char *text = read_file(probe, NULL);
        if (text == NULL) continue;
        int is_ws = strstr(text, "[workspace]") != NULL;
        free(text);
        if (is_ws) {
            snprintf(out, PATH_CAP, "%s", up);
            return 1;
        }
    }
    return 0;
}

/* Reads <dir>/vml.toml: its name, source folder, ignore rules, and (recursively) its [deps] */
static int load_manifest(Project *pr, const char *dir, int is_root, int depth) {
    if (depth > 8) {
        snprintf(pr->error, sizeof(pr->error), "dependencies nest too deeply (a cycle?) at %s", dir);
        return 0;
    }
    char path[PATH_CAP];
    join_path(path, dir, "vml.toml");
    char *text = read_file(path, NULL);
    if (text == NULL) {
        snprintf(pr->error, sizeof(pr->error), "cannot read %s", path);
        return 0;
    }
    Toml t;
    int ok = toml_parse(&t, text, path);
    free(text);
    if (!ok) {
        snprintf(pr->error, sizeof(pr->error), "%s", t.error);
        toml_free(&t);
        return 0;
    }
    char name[64];
    if (!toml_string(toml_get(&t, "project", "name"), name, sizeof(name))) {
        if (toml_get(&t, "workspace", "members") != NULL)
            snprintf(pr->error, sizeof(pr->error), "%s is a workspace: use -p <member>, or run inside a member folder", path);
        else
            snprintf(pr->error, sizeof(pr->error), "%s: [project] needs name = \"...\"", path);
        toml_free(&t);
        return 0;
    }
    const char *bi = toml_get(&t, "project", "builtin");
    int builtin = bi != NULL && strcmp(bi, "true") == 0;
    if (!valid_name(name) || (strcmp(name, "std") == 0) != builtin) {
        snprintf(pr->error, sizeof(pr->error), "%s: project name '%s' must be an identifier, and only the built-in std may be named 'std'",
                 path, name);
        toml_free(&t);
        return 0;
    }
    char src[PATH_CAP], given[PATH_CAP];
    int source_is_root = 0;
    if (toml_string(toml_get(&t, "project", "source"), given, sizeof(given))) {
        if (given[0] == '/' || strstr(given, "..") != NULL) {
            snprintf(pr->error, sizeof(pr->error), "%s: source = \"%s\" must be a folder inside the project", path, given);
            toml_free(&t);
            return 0;
        }
        join_path(src, dir, given);
        source_is_root = strcmp(given, ".") == 0;
    } else {
        join_path(src, dir, "src");
        if (!is_dir(src)) { snprintf(src, sizeof(src), "%s", dir); source_is_root = 1; }
    }
    if (is_root) {
        snprintf(pr->name, sizeof(pr->name), "%s", name);
        if (!toml_string(toml_get(&t, "project", "version"), pr->version, sizeof(pr->version))) snprintf(pr->version, sizeof(pr->version), "0.0.0");
        if (!toml_string(toml_get(&t, "project", "entry"), pr->entry, sizeof(pr->entry))) join_path(pr->entry, src, "main.vml");
    }
    if (!add_mount(pr, name, dir, src, builtin, build_ignore(&t, src, source_is_root))) {
        toml_free(&t);
        return 0;
    }
    for (int i = 0; i < t.count && ok; i++) {
        TomlEntry *e = &t.items[i];
        if (strcmp(e->section, "deps") != 0) continue;
        char dep_path[PATH_CAP], tmp[256], base[PATH_CAP];
        snprintf(base, sizeof(base), "%s", dir);
        if (toml_inline_get(e->value, "git", tmp, sizeof(tmp))) {
            snprintf(pr->error, sizeof(pr->error), "%s:%d: git dependencies are not supported yet (use path = \"...\")", path, e->line);
            ok = 0;
            break;
        }
        if (toml_inline_get(e->value, "as", tmp, sizeof(tmp))) {
            snprintf(pr->error, sizeof(pr->error), "%s:%d: renaming a dependency with as = is not supported yet", path, e->line);
            ok = 0;
            break;
        }
        if (toml_inline_get(e->value, "workspace", tmp, sizeof(tmp)) && strcmp(tmp, "true") == 0) {
            /* `x = { workspace = true }`: the path comes from [workspace.deps] of the enclosing workspace */
            char ws[PATH_CAP], wsfile[PATH_CAP];
            if (!find_workspace(dir, ws)) {
                snprintf(pr->error, sizeof(pr->error), "%s:%d: '%s' uses workspace = true but no workspace vml.toml is above it", path, e->line, e->key);
                ok = 0;
                break;
            }
            join_path(wsfile, ws, "vml.toml");
            char *wtext = read_file(wsfile, NULL);
            Toml wt;
            int wok = wtext != NULL && toml_parse(&wt, wtext, wsfile);
            free(wtext);
            const char *raw = wok ? toml_get(&wt, "workspace.deps", e->key) : NULL;
            int got_path = raw != NULL && toml_inline_get(raw, "path", tmp, sizeof(tmp));
            if (wok) toml_free(&wt);
            if (!got_path) {
                snprintf(pr->error, sizeof(pr->error), "%s:%d: '%s' is not listed in [workspace.deps] of %s", path, e->line, e->key, wsfile);
                ok = 0;
                break;
            }
            snprintf(base, sizeof(base), "%s", ws);
        } else if (!toml_inline_get(e->value, "path", tmp, sizeof(tmp))) {
            snprintf(pr->error, sizeof(pr->error), "%s:%d: dependency '%s' needs { path = \"...\" }", path, e->line, e->key);
            ok = 0;
            break;
        }
        join_path(dep_path, base, tmp);
        char dep_manifest[PATH_CAP];
        join_path(dep_manifest, dep_path, "vml.toml");
        if (!file_exists(dep_manifest)) {
            snprintf(pr->error, sizeof(pr->error), "%s:%d: dependency '%s': no vml.toml in %s", path, e->line, e->key, dep_path);
            ok = 0;
            break;
        }
        int before = pr->num_mounts;
        if (!load_manifest(pr, dep_path, 0, depth + 1)) {
            ok = 0;
            break;
        }
        /* The key must be the dependency's own name (it is the root its modules declare) */
        const char *got = (pr->num_mounts > before) ? pr->mounts[before].name : NULL;
        for (int m = 0; got == NULL && m < pr->num_mounts; m++) {
            if (strcmp(pr->mounts[m].dir, dep_path) == 0) got = pr->mounts[m].name;
        }
        if (got != NULL && strcmp(got, e->key) != 0) {
            snprintf(pr->error, sizeof(pr->error), "%s:%d: dependency '%s' is named '%s' in its vml.toml", path, e->line, e->key, got);
            ok = 0;
            break;
        }
    }
    toml_free(&t);
    return ok;
}

static int open_project(Project *pr) {
    memset(pr, 0, sizeof(*pr));
    if (!enter_project_root()) {
        fprintf(stderr, "vml: no vml.toml here or in any parent folder (create one with: vml new <name>)\n");
        return 0;
    }
    if (!load_manifest(pr, ".", 1, 0)) {
        fprintf(stderr, "vml: %s\n", pr->error);
        free_project(pr);
        return 0;
    }
    return 1;
}

static void mount_all(VML_State *L, const Project *pr) {
    for (int i = 0; i < pr->num_mounts; i++) {
        if (pr->mounts[i].builtin) continue;
        vml_mount_package(L, pr->mounts[i].name, pr->mounts[i].src);
    }
}

/* Every source .vml file under a package (ignore rules applied), in sorted order */
static void hash_sources(uint64_t *h, const Mount *m, const char *rel) {
    char dir[PATH_CAP];
    if (rel[0] == '\0') snprintf(dir, sizeof(dir), "%s", m->src);
    else join_path(dir, m->src, rel);
    int n = 0;
    char **names = list_dir(dir, &n);
    for (int i = 0; i < n; i++) {
        char sub[PATH_CAP], full[PATH_CAP];
        if (rel[0] == '\0') snprintf(sub, sizeof(sub), "%s", names[i]);
        else join_path(sub, rel, names[i]);
        join_path(full, m->src, sub);
        int d = is_dir(full);
        if (m->ign && ign_skip(m->ign, sub, d)) continue;
        if (d) hash_sources(h, m, sub);
        else if (ends_with(names[i], ".vml")) hash_file(h, full);
    }
    if (names) free_list(names, n);
}

static uint64_t project_key(const Project *pr, const char *argv0) {
    uint64_t h = 1469598103934665603ULL;
    hash_str(&h, "vml-cache-v2");
    hash_self(&h, argv0);
    hash_str(&h, pr->entry);
    hash_file(&h, pr->entry);
    for (int i = 0; i < pr->num_mounts; i++) {
        char p[PATH_CAP];
        hash_str(&h, pr->mounts[i].name);
        join_path(p, pr->mounts[i].dir, "vml.toml");
        hash_file(&h, p);
        if (!pr->mounts[i].builtin) hash_sources(&h, &pr->mounts[i], "");
    }
    return h;
}

#define CACHE_DIR   ".vml-cache"
#define CACHE_IMAGE ".vml-cache/main.vmb"
#define CACHE_KEY   ".vml-cache/main.key"

/* Members of the workspace in the current folder (globs like "tools/<all>" expanded); returns count */
static int workspace_members(char out[][PATH_CAP], int cap) {
    char *text = read_file("vml.toml", NULL);
    if (text == NULL) return 0;
    Toml t;
    int ok = toml_parse(&t, text, "vml.toml");
    free(text);
    if (!ok) return 0;
    int n = 0;
    char item[512];
    for (int i = 0; n < cap && toml_list_item(toml_get(&t, "workspace", "members"), i, item, sizeof(item)); i++) {
        size_t il = strlen(item);
        if (il >= 2 && strcmp(item + il - 2, "/*") == 0) {
            item[il - 2] = '\0';
            int k = 0;
            char **names = list_dir(item, &k);
            for (int j = 0; j < k && n < cap; j++) {
                char d[PATH_CAP], m[PATH_CAP];
                join_path(d, item, names[j]);
                join_path(m, d, "vml.toml");
                if (file_exists(m)) snprintf(out[n++], PATH_CAP, "%s", d);
            }
            if (names) free_list(names, k);
        } else {
            snprintf(out[n++], PATH_CAP, "%s", item);
        }
    }
    toml_free(&t);
    return n;
}

static const char *flag_value(int argc, char **argv, const char *flag) {
    for (int i = 2; i + 1 < argc; i++) if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return NULL;
}

/* Opens the project to act on: the nearest vml.toml, or with `-p name` a member of the workspace */
static int open_target(Project *pr, int argc, char **argv) {
    const char *member = flag_value(argc, argv, "-p");
    if (member != NULL) {
        if (!enter_project_root()) {
            fprintf(stderr, "vml: no vml.toml here or in any parent folder\n");
            return 0;
        }
        char members[32][PATH_CAP];
        int n = workspace_members(members, 32);
        for (int i = 0; i < n; i++) {
            char m[PATH_CAP], name[64] = "";
            join_path(m, members[i], "vml.toml");
            char *text = read_file(m, NULL);
            Toml t;
            if (text != NULL && toml_parse(&t, text, m)) {
                toml_string(toml_get(&t, "project", "name"), name, sizeof(name));
                toml_free(&t);
            }
            free(text);
            if (strcmp(name, member) == 0 || strcmp(members[i], member) == 0) {
                if (chdir(members[i]) != 0) break;
                return open_project(pr);
            }
        }
        fprintf(stderr, "vml: no workspace member named '%s'\n", member);
        return 0;
    }
    return open_project(pr);
}

static int compile_to(const Project *pr, const char *out) {
    VML_State *L = vml_new(NULL);
    mount_all(L, pr);
    if (vml_load_file(L, pr->entry) != VML_OK) {
        printf("%s\n", vml_last_error(L));
        vml_free(L);
        return 0;
    }
    if (!vml_has_function(L, "main")) {
        printf("[Compile Error] %s: missing entry point 'func main()'\n", pr->entry);
        vml_free(L);
        return 0;
    }
    VML_Result r = vml_save_image_file(L, out);
    if (r != VML_OK) printf("%s\n", vml_last_error(L));
    vml_free(L);
    return r == VML_OK;
}

/* Brings .vml-cache/main.vmb up to date; *hit = reused without compiling */
static int ensure_built(const Project *pr, const char *argv0, int *hit) {
    char key[32];
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)project_key(pr, argv0));
    *hit = 0;
    char *old = read_file(CACHE_KEY, NULL);
    if (old != NULL && strcmp(trim(old), key) == 0 && file_exists(CACHE_IMAGE)) {
        free(old);
        *hit = 1;
        return 1;
    }
    free(old);
    mkdir(CACHE_DIR, 0755);
    remove(CACHE_KEY);   /* never leave a key that points at a half-written image */
    if (!compile_to(pr, CACHE_IMAGE)) return 0;
    return write_file(CACHE_KEY, key);
}

static int run_image(const char *path, int argc, const char *const *argv) {
    VML_State *L = vml_new(NULL);
    vml_set_args(L, argc, argv);
    VML_Result r = vml_load_image_file(L, path);
    if (r == VML_OK) r = vml_run_main(L);
    int code = 0;
    if (r == VML_OK && vml_exit_requested(L, &code)) {
        vml_free(L);
        return code;
    }
    if (r != VML_OK) {
        printf("[Runtime Error] %s\n", vml_last_error(L));
        vml_free(L);
        return 1;
    }
    vml_free(L);
    return 0;
}

static int has_flag(int argc, char **argv, const char *flag) {
    for (int i = 2; i < argc; i++) if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Commands
 * ------------------------------------------------------------------------- */

int project_cmd_run(int argc, char **argv) {
    Project pr;
    if (!open_target(&pr, argc, argv)) return 1;
    int hit = 0;
    if (!ensure_built(&pr, argv[0], &hit)) return 1;
    if (has_flag(argc, argv, "-v")) fprintf(stderr, "vml: %s\n", hit ? "up to date (cached build)" : "compiled");
    int first = argc;   /* program args: everything after `--` */
    for (int i = 2; i < argc; i++) if (strcmp(argv[i], "--") == 0) { first = i + 1; break; }
    return run_image(CACHE_IMAGE, argc - first, (const char *const *)(argv + first));
}

int project_cmd_build(int argc, char **argv) {
    Project pr;
    const char *out = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
    }
    char cwd_before[PATH_CAP], out_abs[PATH_CAP * 2];
    if (out != NULL && out[0] != '/' && getcwd(cwd_before, sizeof(cwd_before)) != NULL) {
        snprintf(out_abs, sizeof(out_abs), "%s/%s", cwd_before, out);
        out = out_abs;
    }
    if (!open_target(&pr, argc, argv)) return 1;
    int hit = 0;
    if (!ensure_built(&pr, argv[0], &hit)) return 1;
    char dist[PATH_CAP];
    if (out == NULL) {
        mkdir("dist", 0755);
        snprintf(dist, sizeof(dist), "dist/%s", pr.name);
        out = dist;
    }
    /* A runnable program, unless the name asks for a bytecode image (*.vmb) */
    if (ends_with(out, ".vmb")) {
        if (!copy_file(CACHE_IMAGE, out)) {
            fprintf(stderr, "vml: cannot write %s\n", out);
            return 1;
        }
    } else if (!cli_write_executable(CACHE_IMAGE, out, argv[0])) {
        return 1;
    }
    printf("built %s%s\n", out, hit ? " (up to date)" : "");
    return 0;
}

/* Runs the .vml files in tests/ of the project in the current folder; adds to the counters */
static int test_project_here(const Project *pr, int *passed, int *total) {
    int n = 0;
    char **names = list_dir("tests", &n);
    for (int i = 0; i < n; i++) {
        if (!ends_with(names[i], ".vml")) continue;
        char path[PATH_CAP];
        join_path(path, "tests", names[i]);
        (*total)++;
        VML_State *L = vml_new(NULL);
        mount_all(L, pr);
        VML_Result r = vml_load_file(L, path);
        if (r == VML_OK) r = vml_run_main(L);
        if (r == VML_OK) {
            (*passed)++;
            printf("ok    %s/%s\n", pr->name, path);
        } else {
            printf("FAIL  %s/%s\n      %s\n", pr->name, path, vml_last_error(L));
        }
        vml_free(L);
    }
    if (names) free_list(names, n);
    return 1;
}

int project_cmd_test(int argc, char **argv) {
    Project pr;
    int passed = 0, total = 0;
    int whole_workspace = 0;
    if (flag_value(argc, argv, "-p") == NULL && enter_project_root()) {
        char *text = read_file("vml.toml", NULL);
        whole_workspace = text != NULL && strstr(text, "[workspace]") != NULL && strstr(text, "[project]") == NULL;
        free(text);
    }
    if (whole_workspace) {
        char members[32][PATH_CAP], root[PATH_CAP];
        int n = workspace_members(members, 32);
        if (getcwd(root, sizeof(root)) == NULL) return 1;
        for (int i = 0; i < n; i++) {
            if (chdir(members[i]) != 0 || !open_project(&pr)) {
                printf("FAIL  member %s: cannot open it\n", members[i]);
                total++;
                if (chdir(root) != 0) return 1;
                continue;
            }
            test_project_here(&pr, &passed, &total);
            free_project(&pr);
            if (chdir(root) != 0) return 1;
        }
    } else {
        if (!open_target(&pr, argc, argv)) return 1;
        test_project_here(&pr, &passed, &total);
        free_project(&pr);
    }
    if (total == 0) {
        printf("no tests in tests/\n");
        return 0;
    }
    printf("%d/%d test files passed\n", passed, total);
    return passed == total ? 0 : 1;
}

int project_cmd_clean(int argc, char **argv) {
    (void)argc; (void)argv;
    Project pr;
    if (!open_project(&pr)) return 1;
    int n = 0;
    char **names = list_dir(CACHE_DIR, &n);
    for (int i = 0; i < n; i++) {
        char p[PATH_CAP];
        join_path(p, CACHE_DIR, names[i]);
        remove(p);
    }
    if (names) free_list(names, n);
    rmdir(CACHE_DIR);
    printf("removed %s\n", CACHE_DIR);
    return 0;
}

int project_cmd_new(int argc, char **argv) {
    if (argc < 3 || !valid_name(argv[2]) || strcmp(argv[2], "std") == 0) {
        fprintf(stderr, "usage: vml new <name>   (name: letters, digits, _; not 'std')\n");
        return 1;
    }
    const char *name = argv[2];
    if (file_exists(name)) {
        fprintf(stderr, "vml: '%s' already exists\n", name);
        return 1;
    }
    char p[PATH_CAP], text[2048];
    if (mkdir(name, 0755) != 0) {
        fprintf(stderr, "vml: cannot create folder '%s'\n", name);
        return 1;
    }
    join_path(p, name, "src");   mkdir(p, 0755);
    join_path(p, name, "tests"); mkdir(p, 0755);

    snprintf(text, sizeof(text),
             "[project]\nname    = \"%s\"\nversion = \"0.1.0\"\nentry   = \"src/main.vml\"\n\n"
             "[deps]\n# other = { path = \"../other\" }\n", name);
    join_path(p, name, "vml.toml");
    write_file(p, text);

    snprintf(text, sizeof(text),
             "import %s.greet\n\nfunc main() {\n    print(greet.hello(\"world\"))\n}\n", name);
    join_path(p, name, "src/main.vml");
    write_file(p, text);

    snprintf(text, sizeof(text),
             "module %s.greet\n\npub func hello(who: string): string {\n    return \"Hello, \" + who + \"!\"\n}\n", name);
    join_path(p, name, "src/greet.vml");
    write_file(p, text);

    snprintf(text, sizeof(text),
             "import %s.greet\n\nfunc main() {\n    assert(greet.hello(\"vml\") == \"Hello, vml!\", \"greet.hello\")\n}\n", name);
    join_path(p, name, "tests/greet_test.vml");
    write_file(p, text);

    join_path(p, name, ".gitignore");
    write_file(p, ".vml-cache/\ndist/\n");

    printf("created %s/ — cd %s && vml run\n", name, name);
    return 0;
}
