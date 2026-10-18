#ifndef VM_OPT_FUSE_H
#define VM_OPT_FUSE_H

#include "../memory/vm_memory.h"

/*
 * ============================================================================
 * VM SUPERINSTRUCTION FUSER (`vm/opt/vm_fuse.h`)
 * Rewrites hot instruction sequences in place into fused opcodes after
 * compilation. Code size and jump offsets never change.
 * ============================================================================
 */

void vm_fuse_superinstructions(VMChunk *chunk, vm_u32 from_ip, vm_u32 to_ip);

#endif /* VM_OPT_FUSE_H */
