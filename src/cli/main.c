/*
 * `vml` — the single front door (SPEC 6c). Commands live in their own files; this file only
 * dispatches. Unknown commands run an external tool named `vml-<command>` from PATH, so new
 * tools (vml-analyzer, vml-bench, ...) plug in without changing vml.
 */
#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *usage;
    const char *summary;
    int (*run)(int argc, char **argv);
} Command;

static int cmd_build(int argc, char **argv) {
    for (int i = 2; i < argc; i++) {
        size_t n = strlen(argv[i]);
        if (n > 4 && strcmp(argv[i] + n - 4, ".vml") == 0) return cli_build_script(argc, argv);
    }
    return project_cmd_build(argc, argv);
}

static int cmd_help(int argc, char **argv);

static const Command COMMANDS[] = {
    {"new",       "vml new <name>",                       "create a project folder (vml.toml, src/, tests/)", project_cmd_new},
    {"run",       "vml run [-v] [-p member] [-- program args]",             "build what changed (cached in .vml-cache), then run", project_cmd_run},
    {"build",     "vml build [-o file] [-p member]\n      vml build app.vml [-o file] [--lib-path dir] [--pkg name=dir]\n"
                  "  output is a program you run directly (./dist/<name>, ./app); name it *.vmb for a bytecode image",
                                                          "build a runnable program (project -> dist/<name>, script -> ./<name>)", cmd_build},
    {"test",      "vml test [-p member]",                 "run tests/*.vml (every member when run at a workspace root)", project_cmd_test},
    {"clean",     "vml clean",                            "remove .vml-cache", project_cmd_clean},
    {"interface", "vml interface <file.vmb | file.vml>",  "print the public declarations", cli_interface},
    {"help",      "vml help [command]",                   "show this help, or one command's usage", cmd_help},
};
#define NUM_COMMANDS ((int)(sizeof(COMMANDS) / sizeof(COMMANDS[0])))

static int cmd_help(int argc, char **argv) {
    if (argc >= 3) {
        for (int i = 0; i < NUM_COMMANDS; i++) {
            if (strcmp(argv[2], COMMANDS[i].name) == 0) {
                printf("usage: %s\n  %s\n", COMMANDS[i].usage, COMMANDS[i].summary);
                return 0;
            }
        }
        fprintf(stderr, "vml: no command '%s' (see: vml help)\n", argv[2]);
        return 2;
    }
    printf("usage: vml <command> [args]     or     vml <script.vml | image.vmb> [flags]\n\ncommands:\n");
    for (int i = 0; i < NUM_COMMANDS; i++) printf("  %-10s %s\n", COMMANDS[i].name, COMMANDS[i].summary);
    printf("\nother tools: `vml <name>` runs `vml-<name>` from PATH\n"
           "script: vml app.vml [flags] [program args | -- program args]\n"
           "script flags: --time --disasm --ast --parse-only --gc-stress --no-gc --no-io --no-coroutine --no-random\n"
           "              --lib-path <dir> --pkg <name=dir> --deny <module | pkg.*>\n");
    return 0;
}

static int looks_like_command(const char *s) {
    if (*s == '\0' || *s == '-' || strchr(s, '/') != NULL || strchr(s, '.') != NULL) return 0;
    for (const char *p = s; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    /* A program built with `vml build`: run it, every argument is the program's */
    int embedded = cli_run_embedded(argc, argv);
    if (embedded >= 0) return embedded;
    if (argc < 2) return cmd_help(argc, argv);
    for (int i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[1], COMMANDS[i].name) == 0) return COMMANDS[i].run(argc, argv);
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) return cmd_help(argc, argv);
    if (cli_is_image_file(argv[1])) {
        int skip = (argc > 2 && strcmp(argv[2], "--") == 0) ? 3 : 2;
        return cli_run_image(argv[1], argc - skip, (const char *const *)(argv + skip));
    }
    if (access(argv[1], F_OK) == 0 || !looks_like_command(argv[1])) return cli_run_script(argc, argv);

    /* External tool: vml-<name> from PATH gets the remaining arguments */
    char tool[128];
    snprintf(tool, sizeof(tool), "vml-%s", argv[1]);
    argv[1] = tool;
    execvp(tool, argv + 1);
    fprintf(stderr, "vml: unknown command '%s' (see: vml help; external tools are found as %s in PATH)\n", tool + 4, tool);
    return 2;
}
