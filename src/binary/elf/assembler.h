#ifndef NATURE_ASSEMBLER_H
#define NATURE_ASSEMBLER_H

#include "linker.h"
#include "utils/list.h"
#include "src/structs.h"

/**
 * - 编译指令
 */
void object_load_operations(elf_context *ctx, closure_t *c);


/**
 * 变量声明编码
 * @param ctx
 * @param asm_symbols
 */
void object_load_symbols(elf_context *ctx, slice_t *asm_symbols);

#endif //NATURE_ASSEMBLER_H
