#ifndef NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ASM_H_

#include "src/value.h"
//#include "opcode.h"

// 指令字符宽度
#define BYTE 1 // 1 byte
#define WORD 2 // 2 byte
#define DWORD 4 // 4 byte
#define QWORD 8 // 8 byte
#define OWORD 16
#define YWORD 32
#define ZWORD 64

typedef enum {
  ASM_OPERAND_TYPE_REGISTER,
  ASM_OPERAND_TYPE_INDIRECT_REGISTER,
  ASM_OPERAND_TYPE_SIB_REGISTER,
  ASM_OPERAND_TYPE_RIP_RELATIVE,
  ASM_OPERAND_TYPE_DISP_REGISTER,
  ASM_OPERAND_TYPE_UINT8,
  ASM_OPERAND_TYPE_UINT16,
  ASM_OPERAND_TYPE_UINT32,
  ASM_OPERAND_TYPE_UINT64,
  ASM_OPERAND_TYPE_INT32,
  ASM_OPERAND_TYPE_FLOAT32,
  ASM_OPERAND_TYPE_FLOAT64,
} asm_operand_type;

typedef struct {
  uint8_t value;
} asm_operand_uint8;

typedef struct {
  uint16_t value;
} asm_operand_uint16;

typedef struct {
  uint32_t value;
} asm_operand_uint32;

typedef struct {
  uint64_t value;
} asm_operand_uint64;

typedef struct {
  int32_t value;
} asm_operand_int32;


typedef struct {
  float value;
} asm_operand_float32;

typedef struct {
  double value;
} asm_operand_float64;


/**
 * 汇编指令参数
 */
typedef struct {
  string name;
  uint8_t index; // index 对应 intel 手册表中的索引，可以直接编译进 modrm 中
  uint8_t size;
} asm_operand_register; // size 是个啥？

typedef struct {
  asm_operand_register base;
  asm_operand_register index;
  uint8_t scale;
} asm_operand_sib_register;

typedef struct {
  asm_operand_register reg;
} asm_operand_indirect_register; // (%rax)

typedef struct {
  asm_operand_register reg;
  uint8_t disp;
} asm_operand_disp_register;

typedef struct {
  int32_t disp;
} asm_operand_rip_relative;

typedef struct {
  uint8_t type;
  uint8_t size;
  void *value; // asm_operand_register
} asm_operand_t;

/**
 * 汇编指令结构(即如何编写汇编指令)
 */
typedef struct {
  string name; // 指令名称
  uint8_t asm_operand_count;
  asm_operand_t *asm_operands[4]; // 最多 4 个参数
} asm_inst_t;

/**
 * 基于 asm_op + inst_t 的指令选择
 */
//inst_t opcode_select();

// 指令填充, 基于 inst + asm_op 生成 inst_format_t
//inst_format_t inst_gen(inst_t inst); //


#endif //NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
