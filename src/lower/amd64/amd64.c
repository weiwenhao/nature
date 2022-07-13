#include "amd64.h"
#include "src/type.h"
#include "src/assembler/amd64/register.h"
#include "src/lib/error.h"

amd64_lower_fn amd64_lower_table[] = {
        [LIR_OP_TYPE_ADD] = amd64_lower_add,
        [LIR_OP_TYPE_CALL] = amd64_lower_call,
        [LIR_OP_TYPE_BUILTIN_CALL] = amd64_lower_call,
        [LIR_OP_TYPE_RUNTIME_CALL] = amd64_lower_call,
};

/**
 * LIR_OP_TYPE_CALL base, param => result
 * base 有哪些形态？
 * http.get() // 外部符号引用,无非就是是否在符号表中
 * sum.n() // 内部符号引用, sum.n 是否属于 var? 所有的符号都被记录为 var
 * test[0]() // 经过计算后的左值，其值最终存储在了一个临时变量中 => temp var
 * 所以 base 最终只有一个形态，那就是 var, 如果 var 在当前 closure 有定义，那其至少会被
 * - 会被堆栈分配,此时如何调用 call 指令？
 * asm call + stack[1]([indirect addr]) or asm call + reg ?
 * 没啥可以担心的，这都是被支持的
 *
 * - 但是如果其如果没有被寄存器分配，就属于外部符号(label)
 * asm call + SYMBOL 即可
 * 所以最终
 * @param op
 * @param count
 * @return
 */
list *amd64_lower_call(closure *c, lir_op *op) {
    list *insts = list_new();

    asm_operand_t *result = NULL;
    if (op->result != NULL) {
        result = amd64_lower_to_asm_operand(op->result); // 可能是寄存器，也可能是内存地址
    }

    // 特殊逻辑，如果响应的参数是一个结构体，就需要做隐藏参数的处理
    // 实参传递(封装一个 static 函数处理),
    asm_operand_t *first = amd64_lower_to_asm_operand(op->first);

    uint8_t next_param = 0;
    // 1. 大返回值处理(使用 rdi 预处理)
    if (op->data_type == TYPE_STRUCT && result != NULL) {
        reg_t *target_reg = amd64_lower_next_actual_reg_target(&next_param, QWORD);
        list_push(insts, ASM_INST("lea", { REG(target_reg), result }));
    }

    // 2. 参数处理  lir_ope op->second;
    lir_operand_actual_param *v = op->second->value;
    list *param_insts = list_new();
    for (int i = 0; i < v->count; ++i) {
        lir_operand *operand = v->list[i];
        // 实参可能是简单参数，也有可能是复杂参数
        regs_t used_regs = {.count = 0};
        asm_operand_t *source = NEW(asm_operand_t);
        list *temp = amd64_lower_complex_to_asm_operand(operand, source, &used_regs);
        reg_t *target_reg = amd64_lower_next_actual_reg_target(&next_param, source->size); // source 和 target 大小要匹配
        if (target_reg == NULL) {
            // push
            list_push(temp, ASM_INST("push", { source }));
        } else {
            list_push(temp, ASM_INST("mov", { REG(target_reg), source }));
        }
        list_merge(temp, param_insts);
    }

    list_merge(insts, param_insts);

    // 3. 调用 call 指令(处理地址)
    list_push(insts, ASM_INST("call", { first }));

    // 4. 响应处理(取出响应值传递给 result)
    if (result != NULL) {
        if (op->data_type == TYPE_FLOAT) {
            list_push(insts, ASM_INST("mov", { REG(xmm0), result }));
        } else {
            list_push(insts, ASM_INST("mov", { REG(rax), result }));
        }
    }

    return insts;
}

/**
 * 核心问题是: 在结构体作为返回值时，当外部调用将函数的返回地址作为参数 rdi 传递给函数时，
 * 根据 ABI 规定，函数操作的第一步就是对 rdi 入栈，但是当 lower return 时,我并不知道 rdi 被存储在了栈的什么位置？
 * 但是实际上是能够知道的，包括初始化时 sub rbp,n 的 n 也是可以在寄存器分配阶段就确定下来的。
 * n 的信息作为 closure 的属性存储在 closure 中，如何将相关信息传递给 lower ?, 参数 1 改成 closure?
 * 在结构体中 temp_var 存储的是结构体的起始地址。不能直接 return 起始地址，大数据会随着函数栈帧消亡。 而是将结构体作为整个值传递。
 * 既然已经知道了结构体的起始位置，以及隐藏参数的所在栈帧。 就可以直接进行结构体返回值的构建。
 * @param c
 * @param ast
 * @return
 */
list *amd64_lower_return(closure *c, lir_op *op) {
    // 编译时总是使用了一个 temp var 来作为 target, 所以这里进行简单转换即可
    list *inst_list = list_new();

    asm_operand_t *result = amd64_lower_to_asm_operand(op->result);

    if (op->data_type == TYPE_STRUCT) {
        // 计算长度
        int rep_count = ceil((float) op->size / QWORD);
        asm_operand_t *return_disp_rbp = DISP_REG(rbp, c->return_offset);

        list_push(inst_list, ASM_INST("mov", { REG(rsi), result }));
        list_push(inst_list, ASM_INST("mov", { REG(rdi), return_disp_rbp }));
        list_push(inst_list, ASM_INST("mov", { REG(rcx), UINT64(rep_count) }));
        list_push(inst_list, MOVSQ(0xF3));

        list_push(inst_list, ASM_INST("mov", { REG(rax), REG(rdi) }));
    } else if (op->data_type == TYPE_FLOAT) {
        list_push(inst_list, ASM_INST("mov", { REG(xmm0), result }));
    } else {
        list_push(inst_list, ASM_INST("mov", { REG(rax), result }));
    }

    // TODO jmp function_ret

    return inst_list;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
list *amd64_lower_add(closure *c, lir_op *op) {
    if (op->data_type == TYPE_INT) {
        list *inst_list = list_new();
        regs_t used_regs = {.count = 0};

        // 参数转换
        asm_operand_t *first = NEW(asm_operand_t);
        list *temp = amd64_lower_complex_to_asm_operand(op->first, first, &used_regs);
        list_merge(inst_list, temp);
        asm_operand_t *second = NEW(asm_operand_t);
        temp = amd64_lower_complex_to_asm_operand(op->second, second, &used_regs);
        list_merge(inst_list, temp);

        asm_operand_t *result = NEW(asm_operand_t);
        temp = amd64_lower_complex_to_asm_operand(op->result, result, &used_regs);
        list_merge(inst_list, temp);

        reg_t *reg = amd64_lower_next_reg(&used_regs, op->size);
        list_push(inst_list, ASM_INST("mov", { REG(reg), first }));
        list_push(inst_list, ASM_INST("add", { REG(reg), second }));
        list_push(inst_list, ASM_INST("mov", { result, REG(reg) }));

        return inst_list;
    }
    return NULL;
}

/**
 * mov foo => bar
 * mov 1 => var
 * mov 1.1 => var
 * @param c
 * @param op
 * @param count
 * @return
 */
list *amd64_lower_mov(closure *c, lir_op *op) {
    list *inst_list = list_new();

    // 结构体处理
    if (op->data_type == TYPE_STRUCT) {
//    regs_t used_regs = {.count = 0};
        // first => result
        // 如果操作数是内存地址，则直接 lea, 如果操作数是寄存器，则不用操作
        // lea first to rax
        asm_operand_t *first_reg = REG(rax);
        asm_operand_t *first = amd64_lower_to_asm_operand(op->first);
        if (first->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
            list_push(inst_list, ASM_INST("lea", { REG(rax), first }));
        } else {
            first_reg = first;
        }
        // lea result to rdx
        asm_operand_t *result_reg = REG(rdx);
        asm_operand_t *result = amd64_lower_to_asm_operand(op->result);
        if (result->type == ASM_OPERAND_TYPE_DISP_REGISTER) {
            list_push(inst_list, ASM_INST("lea", { REG(rdx), result }));
        } else {
            result_reg = result;
        }

        // mov rax,rsi, mov rdx,rdi, mov count,rcx
        int rep_count = ceil((float) op->size / QWORD);
        list_push(inst_list, ASM_INST("mov", { REG(rsi), first_reg }));
        list_push(inst_list, ASM_INST("mov", { REG(rdi), result_reg }));
        list_push(inst_list, ASM_INST("mov", { REG(rcx), UINT64(rep_count) }));
        list_push(inst_list, MOVSQ(0xF3));
        return inst_list;
    }

    regs_t used_regs = {.count = 0};

    // 参数转换
    asm_operand_t *first = NEW(asm_operand_t);
    list *temp = amd64_lower_complex_to_asm_operand(op->first, first, &used_regs);
    list_merge(inst_list, temp);

    asm_operand_t *result = NEW(asm_operand_t);
    temp = amd64_lower_complex_to_asm_operand(op->result, result, &used_regs);
    list_merge(inst_list, temp);

    reg_t *reg = amd64_lower_next_reg(&used_regs, op->data_type);
    list_push(inst_list, ASM_INST("mov", { REG(reg), first }));
    list_push(inst_list, ASM_INST("mov", { result, REG(reg) }));

    return inst_list;
}

asm_operand_t *amd64_lower_to_asm_operand(lir_operand *operand) {
    if (operand->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *v = operand->value;
        if (v->stack_frame_offset > 0) {
            return DISP_REG(rbp, v->stack_frame_offset);
        }
        if (v->reg_id > 0) {
            asm_operand_register_t *reg = (asm_operand_register_t *) physical_regs.list[v->reg_id];
            return REG(reg);
        }

        return SYMBOL(v->ident, v->is_label, false);
//    error_exit("[amd64_lower_to_asm_operand] type var not a reg or stack_frame_offset");
    }

    // 简单立即数
    if (operand->type == LIR_OPERAND_TYPE_IMMEDIATE) {
        lir_operand_immediate *v = operand->value;
        if (v->type == TYPE_INT) {
            return UINT64(v->int_value);
        }
        if (v->type == TYPE_FLOAT) {
            return FLOAT32(v->float_value);
        }
        if (v->type == TYPE_BOOL) {
            return UINT8(v->bool_value);
        }
        error_exit("[amd64_lower_to_asm_operand] type immediate not expected");
    }

    // label(都是局部 label)
    if (operand->type == LIR_OPERAND_TYPE_LABEL) {
        lir_operand_label *v = operand->value;
        return SYMBOL(v->ident, true, true);
    }

    error_exit("[amd64_lower_to_asm_operand] operand type not ident");
    return NULL;
}

/**
 * used_regs 围绕着单个指令进行
 * @param operand
 * @param asm_operand
 * @param used_regs
 * @return
 */
list *amd64_lower_complex_to_asm_operand(lir_operand *operand,
                                         asm_operand_t *asm_operand,
                                         regs_t *used_regs) {
    list *inst_list = list_new();

    if (operand->type == LIR_OPERAND_TYPE_IMMEDIATE) {
        lir_operand_immediate *v = operand->value;
        if (v->type == TYPE_STRING) {
            // 生成符号表
            char *unique_name = AMD64_DECL_UNIQUE_NAME();
            asm_var_decl *decl = NEW(asm_var_decl);
            decl->name = unique_name;
            decl->size = strlen(v->string_value);
            decl->value = (uint8_t *) v->string_value;
            decl->type = ASM_VAR_DECL_TYPE_STRING;
            list_push(amd64_decl_list, decl);

            // 使用临时寄存器保存结果(会增加一条 lea 指令)
            reg_t *reg = amd64_lower_next_reg(used_regs, QWORD);

            list_push(inst_list, ASM_INST("lea", { REG(reg), SYMBOL(unique_name, false, false) }));

            // asm_copy
            ASM_OPERAND_COPY(asm_operand, REG(reg));

            return inst_list;
        }
        // 匹配失败继续往下走,简单转换里面还有一层 imm 匹配
    }

    if (operand->type == LIR_OPERAND_TYPE_MEMORY) {
        lir_operand_memory *v = operand->value;
        // base 类型必须为 var
        if (v->base->type != LIR_OPERAND_TYPE_VAR) {
            error_exit("[amd64_lir_to_asm_operand] operand type memory, but that base not type var");
        }

        lir_operand_var *var = v->base->value;
        // 如果是寄存器类型就直接返回 disp reg operand
        if (var->reg_id > 0) {
            asm_operand_register_t *reg = (asm_operand_register_t *) physical_regs.list[var->reg_id];
            asm_operand_t *temp = DISP_REG(reg, v->offset);
            ASM_OPERAND_COPY(asm_operand, temp);
            free(temp);
            return inst_list;
        }

        if (var->stack_frame_offset > 0) {
            // 需要占用一个临时寄存器
            reg_t *reg = amd64_lower_next_reg(used_regs, QWORD);

            // 生成 mov 指令（asm_mov）
            list_push(inst_list, ASM_INST("mov", { REG(reg), DISP_REG(rbp, var->stack_frame_offset) }));
            asm_operand_t *temp = DISP_REG(reg, v->offset);
            ASM_OPERAND_COPY(asm_operand, temp);
            return inst_list;
        }

        error_exit("[amd64_lir_to_asm_operand]  var cannot reg_id or stack_frame_offset");
    }

    // 按简单参数处理
    asm_operand_t *temp = amd64_lower_to_asm_operand(operand);
    ASM_OPERAND_COPY(asm_operand, temp);
    return inst_list;
}

list *amd64_lower(closure *c, lir_op *op) {
    amd64_lower_fn fn = amd64_lower_table[op->op];
    return fn(c, op);
}

void amd64_lower_init() {
    amd64_decl_list = list_new();
}

reg_t *amd64_lower_next_reg(regs_t *used, uint8_t size) {
    reg_t *r = (reg_t *) amd64_register_find(used->count, size);
    if (r == NULL) {
        error_exit("[amd64_register_find] result null, count: %d, size: %v", used->count, size);
    }
    used->list[used->count++] = r;
    return r;
}


/**
 * 返回下一个可用的寄存器或者内存地址
 * 但是这样就需要占用 rbp 偏移，怎么做？
 * 每个函数定义的最开始已经使用 sub n => rsp, 已经申请了栈空间 [0 ~ n]
 * 后续函数中调用其他函数所使用的栈空间都是在 [n ~ 无限] 中, 直接通过 push 操作即可
 * 但是需要注意 push 的顺序，最后的参数先 push (也就是指令反向 merge)
 * 如果返回了 NULL 就说明没有可用的寄存器啦，加一条 push 就行了
 * @param count
 * @param size
 * @return
 */
reg_t *amd64_lower_next_actual_reg_target(uint8_t used[7], uint8_t size) {
    uint8_t used_index = (uint8_t) log2f(size);
    uint8_t count = used[used_index];
    uint8_t reg_index_list[] = {7, 6, 2, 1, 8, 9};
    // 通用寄存器 (0~5 = 6 个)
    if (size <= QWORD && count <= 5) {
        uint8_t reg_index = reg_index_list[count];
        return (reg_t *) amd64_register_find(reg_index, size);
    }

    // 浮点寄存器(0~7 = 8 个)
    if (size > QWORD && count <= 7) {
        return (reg_t *) amd64_register_find(count, size);
    }

    return NULL;
}
