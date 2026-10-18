#ifndef COMPILER_H
#define COMPILER_H

#include "../vm/core/vm_types.h"
#include "../vm/memory/vm_memory.h"
#include "../vm/embed/vm_embed.h"

/* Compiles source to bytecode with the AST front end (lexer -> parser -> AST -> codegen) */
vm_bool vm_compile_source(const char *source, VMChunk *chunk);
vm_bool vm_compile_source_with_config(const char *source, VMChunk *chunk, const VMConfig *cfg);

/* Legacy single-pass token compiler, kept temporarily for comparison (`--legacy`) */
vm_bool vm_compile_source_legacy(const char *source, VMChunk *chunk, const VMConfig *cfg);

#endif /* COMPILER_H */
