#ifndef COMPILER_STD_SOURCES_H
#define COMPILER_STD_SOURCES_H

/* Standard library modules written in `.vml`, embedded at build time (tools/embed_std.c) */
typedef struct {
    const char *name;     /* module name: `std/<name>.vml` */
    const char *source;
} VMStdSource;

extern const VMStdSource vm_std_sources[];
extern const unsigned    vm_std_num_sources;

#endif /* COMPILER_STD_SOURCES_H */
