#ifndef VM_GC_H
#define VM_GC_H

#include "../memory/vm_memory.h"

/*
 * ============================================================================
 * MARK-SWEEP GARBAGE COLLECTOR (`vm/gc/vm_gc.h`)
 * Collects arrays, maps, runtime strings, closure cells and closures.
 *
 * Registers and cells are untyped, so they are scanned conservatively: any
 * value that could be an object id keeps that object alive. Arrays and maps
 * with a known element type are scanned precisely. Object ids never move
 * (freed slots go on free lists); only string bytes are compacted.
 * Collections run only at VM safepoints (loop back-edges and calls).
 * ============================================================================
 */

/* Marks strings created so far (compile time) as permanent. Called when execution starts. */
void vm_gc_seal(VMChunk *chunk);

/* Runs a full collection now. Only call from a safepoint (no raw string pointers held). */
void vm_gc_collect(VMChunk *chunk);

/* Every VMCPU executing on a chunk is a root set while it runs */
void vm_gc_push_cpu(VMChunk *chunk, void *cpu);
void vm_gc_pop_cpu(VMChunk *chunk);

#endif /* VM_GC_H */
