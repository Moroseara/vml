#ifndef VML_INTERNAL_H
#define VML_INTERNAL_H

#include "../../include/vml.h"
#include "../memory/vm_memory.h"
#include "../cpu/vm_cpu.h"
#include "../embed/vm_embed.h"

/* Internal layout behind the opaque handles in vml.h (not part of the API) */

#define VML_MAX_MODULES      VM_MAX_HOST_MODULES
#define VML_MAX_MOD_FUNCS    64
#define VML_MAX_MOD_CONSTS   32
#define VML_MAX_MOD_TYPES    8

#define VML_MAX_MOD_STRUCTS  16
#define VML_MAX_STRUCT_FIELDS 16

/* A struct declared through the builder (also kept as generated .vml text) */
typedef struct {
    char       name[32];
    char       field_names[VML_MAX_STRUCT_FIELDS][32];
    VMDataType field_types[VML_MAX_STRUCT_FIELDS];
    vm_u8      num_fields;
} VML_StructRec;

typedef struct {
    char          name[32];
    VMDataType    params[VM_MAX_PARAMS];
    vm_u8         num_params;
    VMDataType    ret;
    VML_CFunction fn;
} VML_FuncRec;

typedef struct {
    char       name[32];
    VMDataType type;
    VMReg      val;
    char      *str;            /* owned copy for string constants */
} VML_ConstRec;

struct VML_Module {
    VML_State   *L;
    char         name[32];
    void        *user_data;
    VML_FuncRec  funcs[VML_MAX_MOD_FUNCS];
    vm_u32       num_funcs;
    VML_ConstRec consts[VML_MAX_MOD_CONSTS];
    vm_u32       num_consts;
    char         types[VML_MAX_MOD_TYPES][32];
    vm_u32       num_types;
    VML_StructRec structs[VML_MAX_MOD_STRUCTS];
    vm_u32       num_structs;
    char        *src;              /* generated .vml declarations (builder API), compiled on `import` */
    vm_u32       src_len;
    vm_u32       src_cap;
    vm_bool      building;         /* a struct / enum / function builder is open */
    vm_bool      finished;
};

struct VML_State {
    VMChunk     *chunk;
    VMCPU       *cpu;
    VMConfig     cfg;
    VML_Alloc    alloc;
    void        *alloc_ud;
    VML_Module  *modules[VML_MAX_MODULES];
    vm_u32       num_modules;
    vm_bool      loaded;
    char        *source_copy;      /* program source kept alive for the chunk's lifetime */
    vm_u32       source_len;
    uint64_t     fuel;
    char         error[512];
    VMReg        result;
    VMDataType   result_type;
    VML_PrintFn  print_fn;
    void        *print_ud;
};

struct VML_Call {
    VML_State      *L;
    VMChunk        *chunk;
    const VMReg    *args;
    vm_u8           argc;
    VMFunctionInfo *fn;
    void           *user_data;
    VMReg           ret;
};

void       *vml_mem(VML_State *L, void *ptr, size_t old_size, size_t new_size);
void        vml_set_error(VML_State *L, const char *msg);
VMReg       vml_host_trampoline(VMChunk *chunk, const VMReg *args, vm_u8 argc);
VML_Module *vml_find_module(VML_State *L, const char *name, vm_u32 len);
vm_bool     vml_src_append(VML_Module *m, const char *text, vm_u32 len);
vm_bool     vml_src_append_cstr(VML_Module *m, const char *text);
VML_StructRec *vml_find_struct(VML_Module *m, const char *name, vm_u32 len);
vm_bool     vml_parse_type_word(VML_Module *m, const char **s, VMDataType *out, int *out_struct);

#endif /* VML_INTERNAL_H */
