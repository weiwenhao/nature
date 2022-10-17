#include "interval.h"
#include "src/ssa.h"
#include "utils/list.h"
#include "utils/stack.h"
#include "utils/helper.h"
#include "assert.h"


static interval_t *interval_new_child(closure_t *c, interval_t *i) {
    interval_t *child = interval_new(c);
    interval_t *parent = i;
    if (parent->parent) {
        parent = parent->parent;
    }

    if (i->var) {
        lir_operand_var *var = lir_new_temp_var_operand(c, i->var->decl->type)->value;
        child->var = var;
        table_set(c->interval_table, var->ident, child);
    } else {
        assert(parent->fixed);
        child->fixed = parent->fixed;
        child->assigned = parent->assigned;
        table_set(c->interval_table, alloc_regs[child->assigned]->name, child);
    }

    child->parent = parent;
    child->reg_hint = parent;
    child->alloc_type = parent->alloc_type;
    child->stack_slot = parent->stack_slot;

    return child;
}

static bool resolve_blocked(int8_t *block_regs, interval_t *from, interval_t *to) {
    if (to->spilled) {
        return false;
    }

    if (block_regs[to->assigned] == 0) {
        return false;
    }

    if (block_regs[to->assigned] == 1 && to->assigned == from->assigned) {
        return false;
    }

    return true;
}

static void block_insert_mov(basic_block_t *block, int id, interval_t *src_i, interval_t *dst_i) {
    LIST_FOR(block->operations) {
        lir_op_t *op = LIST_VALUE();
        if (op->id < id) {
            continue;
        }

        // last->id < id < op->id
        lir_operand *dst = LIR_NEW_OPERAND(LIR_OPERAND_VAR, dst_i->var);
        lir_operand *src = LIR_NEW_OPERAND(LIR_OPERAND_VAR, src_i->var);
        lir_op_t *mov_op = lir_op_move(dst, src);
        list_insert_before(block->operations, LIST_NODE(), mov_op);
        return;
    }
}

static void closure_insert_mov(closure_t *c, int insert_id, interval_t *src_i, interval_t *dst_i) {
    SLICE_FOR(c->blocks, basic_block_t) {
        basic_block_t *block = SLICE_VALUE();
        // TODO insert_id 具体在什么位置？
        if (block->first_op->id > insert_id || block->last_op->id < insert_id) {
            continue;
        }

        block_insert_mov(block, insert_id, src_i, dst_i);
    }
}

static interval_t *operand_interval(closure_t *c, lir_operand *operand) {
    if (operand->type == LIR_OPERAND_VAR) {
        lir_operand_var *var = operand->value;
        interval_t *interval = table_get(c->interval_table, var->ident);
        assert(interval);
        return interval;
    }
    if (operand->type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        interval_t *interval = table_get(c->interval_table, reg->name);
        assert(interval);
        return interval;
    }

    return NULL;
}

static slice_t *operand_intervals(closure_t *c, lir_operand *operand) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }

    if (operand->type == LIR_OPERAND_VAR) {
        lir_operand_var *var = operand->value;
        interval_t *interval = table_get(c->interval_table, var->ident);
        assert(interval);
        slice_push(result, interval);
    }

    if (operand->type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        interval_t *interval = table_get(c->interval_table, reg->name);
        assert(interval);
        slice_push(result, interval);
    }

    if (operand->type == LIR_OPERAND_ACTUAL_PARAM) {
        lir_operand_actual_param *operands = operand->value;
        for (int i = 0; i < operands->count; ++i) {
            lir_operand *o = operands->list[i];
            assert(o->type != LIR_OPERAND_VAR && "ACTUAL_PARAM nesting is not allowed");

            if (o->type == LIR_OPERAND_VAR) {
                lir_operand_var *var = operand->value;
                interval_t *interval = table_get(c->interval_table, var->ident);
                assert(interval);
                slice_push(result, interval);
            }
        }
    }

    return result;
}

static slice_t *op_output_intervals(closure_t *c, lir_op_t *op) {
    // output 总是存储在 result 中
    return operand_intervals(c, op->output);
}

static slice_t *op_input_intervals(closure_t *c, lir_op_t *op) {
    slice_t *result = operand_intervals(c, op->first);
    slice_t *temp = operand_intervals(c, op->second);

    SLICE_FOR(temp, interval_t) {
        slice_push(result, SLICE_VALUE());
    }

    return result;
}

/**
 * output 没有特殊情况就必须分一个寄存器，主要是 amd64 的指令基本都是需要寄存器参与的
 * @param c
 * @param op
 * @param i
 * @return
 */
static use_kind_e use_kind_of_output(closure_t *c, lir_op_t *op, interval_t *i) {
    return USE_KIND_MUST;
}

/**
 * output 已经必须要有寄存器了, input 就无所谓了
 * @param c
 * @param op
 * @param i
 * @return
 */
static use_kind_e use_kind_of_input(closure_t *c, lir_op_t *op, interval_t *i) {
    return USE_KIND_SHOULD;
}

static bool in_range(interval_range_t *range, int position) {
    return range->from <= position && position < range->to;
}

// 每个块需要存储什么数据？
// loop flag
// loop index,只存储最内层的循环的 index
// loop depth
// 每个块的前向和后向分支 count
// 当且仅当循环块的开头
// 假如使用广度优先遍历，编号按照广度优先的层级来编号,则可以方便的计算出树的高度,顶层 为 0，然后依次递增
// 使用树的高度来标记 loop_index，如果一个块被标记了两个 index ,则 index 大的为内嵌循环
// 当前层 index 等于父节点 + 1
/**
 * 1. 循环块的访问非常密集，所以在进行 flatten 时，将循环块排在一起，且中间不插入非循环的块
 * 2. 嵌套的定义: 一个 loop 的 header 在另外一个循环中,loop header 支配着循环内的所有 block，
 *    如果 loop header A 支配 loop header B，则 A 和 B 是嵌套循环
 * @param c
 */
void interval_loop_detection(closure_t *c) {
    list *work_list = list_new();
    list_push(work_list, c->entry);
    c->entry->loop.tree_high = 1; // 从 1 开始标号，避免出现 0 = 0 的判断

    slice_t *loop_headers = slice_new();
    slice_t *loop_ends = slice_new();
    int8_t loop_count = 0;

    // 1. 探测出循环头与循环尾部(主要是尾部)
    while (!list_empty(work_list)) {
        basic_block_t *current = list_pop(work_list);
        c->entry->loop.visited = true;
        c->entry->loop.active = true;

        // 是否会出现 succ 的 flag 是 visited?
        // 如果当前块是 visited,则当前块的正向后继一定是 null, 当前块的反向后继一定是 active,不可能是 visited
        // 因为一个块的所有后继都进入到 work_list 之后，才会进行下一次 work_list 提取操作
        slice_t *forward_succs = slice_new();
        slice_t *backward_succs = slice_new();
        for (int i = 0; i < current->succs->count; ++i) {
            basic_block_t *succ = current->succs->take[i];
            succ->loop.tree_high = current->loop.tree_high + 1; // 广度遍历中的 tree_high

            // 如果发现循环, backward branches
            if (succ->loop.active) {
                if (!succ->loop.header) {
                    // 当前 succ 是 loop_headers, loop_headers 的 tree_high 为 loop_index
                    succ->loop.header = true;
                    succ->loop.index = loop_count++;
//                    succ->loop.index_list[succ->loop.depth++] = succ->loop.index; //
                    slice_push(loop_headers, succ);
                }


                // 当前 current 是 loop_ends, loop_ends, index = loop_headers.index
                current->loop.index = succ->loop.index;
//                current->loop.index_list[current->loop.depth++] = succ->loop.index;
                current->loop.end = true;
                slice_push(loop_ends, current);

                current->backward_succ = succ; // current is loop end, succ is loop header
                continue;
            }

            slice_push(forward_succs, succ);
            succ->incoming_forward_count++; // 前驱中正向进入 succ 的数量
            succ->loop.visited = true;
        }

        current->forward_succs = forward_succs;
        // 所有 succ 都处理完毕，清除 active 标志
        current->loop.active = false;
    }

    /**
     * 2. 标号,计算一个循环包含的所有块
     * 这里有一个严肃的问题，如果一个节点有两个前驱时，也能够被标号吗？如果是普通结构不考虑 goto 的情况下，则不会出现这种 cfg
     */
    for (int i = 0; i < loop_ends->count; ++i) {
        basic_block_t *end = loop_ends->take[i];
        list_push(work_list, end);
        table_t *handled = table_new();
        table_set(handled, itoa(end->label_index), end);

        while (!list_empty(work_list)) {
            basic_block_t *current = list_pop(work_list);

            // value: index
            current->loop.index_list[current->loop.depth++] = end->loop.index;


            // loop header 不进行 preds 计算
            if (current == end->backward_succ) {
                // current is loop header
                continue;
            }

            // 基于 preds 向后遍历，找到该循环的所有 current
            for (int k = 0; k < current->preds->count; ++k) {
                basic_block_t *pred = current->preds->take[k];

                // 判断是否已经入过队(标号)
                if (table_exist(handled, itoa(pred->label_index))) {
                    continue;
                }

                list_push(work_list, pred);
                table_set(handled, itoa(current->label_index), current);
            }
        }
    }

    // 3. 遍历所有 basic_block ,通过 loop.index_list 确定 innermost index
    for (int label = 0; label < c->blocks->count; ++label) {
        basic_block_t *block = c->blocks->take[label];
        if (block->loop.index != 0) {
            continue;
        }
        if (block->loop.depth == 0) {
            continue;
        }

        // 值越大，树高越低
        uint8_t index = 0;
        for (int i = 0; i < block->loop.depth; ++i) {
            if (block->loop.index_list[i] > index) {
                index = block->loop.index_list[i];
            }
        }

        block->loop.index = index;
    }
}

// 大值在栈顶被优先处理
static void interval_insert_to_stack_by_depth(stack_t *work_list, basic_block_t *block) {
    // next->next->next
    stack_node *p = work_list->top; // top 指向栈中的下一个可用元素，总是为 NULL
    while (p->next != NULL && ((basic_block_t *) p->next->value)->loop.depth > block->loop.depth) {
        p = p->next;
    }

    // p->next == NULL 或者 p->next 小于等于 当前 block
    // p = 3 block = 2  p_next = 2
    stack_node *last_node = p->next;
    // 初始化一个 node
    stack_node *await_node = stack_new_node(block);
    p->next = await_node;

    if (last_node != NULL) {
        await_node->next = last_node;
    }
}

// 优秀的排序从而构造更短更好的 lifetime interval
// 权重越大排序越靠前
// 权重的本质是？或者说权重越大一个基本块？
void interval_block_order(closure_t *c) {
    stack_t *work_list = stack_new();
    stack_push(work_list, c->entry);

    while (!stack_empty(work_list)) {
        basic_block_t *block = stack_pop(work_list);
        slice_push(c->order_blocks, block);

        // 需要计算每一个块的正向前驱的数量
        for (int i = 0; i < block->forward_succs->count; ++i) {
            basic_block_t *succ = block->forward_succs->take[i];
            succ->incoming_forward_count--;
            if (succ->incoming_forward_count == 0) {
                // sort into work_list by loop.depth, 权重越大越靠前，越先出栈
                interval_insert_to_stack_by_depth(work_list, succ);
            }
        }
    }
}

void interval_mark_number(closure_t *c) {
    int next_id = 0;
    for (int i = 0; i < c->order_blocks->count; ++i) {
        basic_block_t *block = c->order_blocks->take[i];
        list_node *current = list_first(block->operations);

        while (current->value != NULL) {
            lir_op_t *op = current->value;
            if (op->code == LIR_OPCODE_PHI) {
                current = current->succ;
                continue;
            }

            op->id = next_id;
            next_id += 2;
            current = current->succ;
        }
    }
}

void interval_build(closure_t *c) {
    // new_interval for all physical registers
    for (int alloc_id = 0; alloc_id < alloc_reg_count(); ++alloc_id) {
        reg_t *reg = alloc_regs[alloc_id];
        interval_t *interval = interval_new(c);
        interval->fixed = true;
        interval->assigned = alloc_id;
        interval->alloc_type = reg->type;
        table_set(c->interval_table, reg->name, interval_new(c));
    }

    // new interval for all virtual registers in closure
    c->interval_table = table_new();
    for (int i = 0; i < c->globals->count; ++i) {
        lir_operand_var *var = c->globals->take[i];
        interval_t *interval = interval_new(c);
        interval->var = var;
        interval->alloc_type = type_base_trans(var->type_base);
        table_set(c->interval_table, var->ident, interval);
    }

    // 倒序遍历顺序基本块基本块
    for (int i = c->order_blocks->count - 1; i >= 0; --i) {
        basic_block_t *block = c->order_blocks->take[i];
        slice_t *live_in = slice_new();

        // 1. calc live in = union of successor.liveIn for each successor of b
        table_t *union_vars = table_new();
        for (int j = 0; j < block->succs->count; ++j) {
            basic_block_t *succ = block->succs->take[j];
            for (int k = 0; k < succ->live_in->count; ++k) {
                lir_operand_var *var = succ->live_in->take[k];
                live_add(union_vars, live_in, var);
            }
        }

        // 2. phi function phi of successors of b do
        for (int j = 0; j < block->succs->count; ++j) {
            basic_block_t *succ_block = block->succs->take[j];
            list_node *current = list_first(succ_block->operations)->succ;
            while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
                lir_op_t *op = OP(current);
                lir_operand_var *var = ssa_phi_body_of(op->first->value, succ_block->preds, block);
                live_add(union_vars, live_in, var);

                current = current->succ;
            }
        }


        int block_from = OP(list_first(block->operations)->value)->id;
        int block_to = OP(list_first(block->operations)->value)->id + 2; // whether add 2?

        // live in add full range 遍历所有的 live_in(union all succ, so it similar live_out),直接添加最长间隔,后面会逐渐缩减该间隔
        for (int k = 0; k < live_in->count; ++k) {
            interval_add_range(c, block->live_out->take[k], block_from, block_to);
        }

        // 倒序遍历所有块指令
        list_node *current = list_last(block->operations);
        while (current->value != NULL) {
            // 判断是否是 call op,是的话就截断所有物理寄存器
            lir_op_t *op = current->value;

            // fixed all phy reg in call
            if (lir_op_is_call(op)) {
                // traverse all register
                for (int j = 0; j < alloc_reg_count(); ++j) {
                    reg_t *reg = alloc_regs[j];
                    interval_t *interval = table_get(c->interval_table, reg->name);
                    if (interval != NULL) {
                        interval_add_range(c, interval, op->id, op->id + 1);
                        interval_add_use_pos(c, interval, op->id, use_kind_of_output(c, op, interval));
                    }
                }
            }

            // add reg hint for move
            if (op->code == LIR_OPCODE_MOVE) {
                interval_t *src_interval = operand_interval(c, op->first);
                interval_t *dst_interval = operand_interval(c, op->output);
                if (src_interval != NULL && dst_interval != NULL) {
                    dst_interval->reg_hint = src_interval;
                }
            }

            // TODO add reg hint for phi, but phi a lot of input var?

            // interval by output params, so it contain opcode phi
            slice_t *intervals = op_output_intervals(c, op);
            for (int j = 0; j < intervals->count; ++j) {
                interval_t *interval = intervals->take[j];
                if (interval->fixed) {
                    interval_add_range(c, interval, op->id, op->id + 1);
                } else {
                    live_remove(union_vars, live_in, interval->var);
                    interval->first_range->from = op->id;
                }

                interval_add_use_pos(c, interval, op->id, use_kind_of_output(c, op, interval));
            }

            intervals = op_input_intervals(c, op);
            for (int j = 0; j < intervals->count; ++j) {
                interval_t *interval = intervals->take[j];
                if (interval->fixed) {
                    interval_add_range(c, interval, op->id, op->id + 1);
                } else {
                    interval_add_range(c, interval, block_from, op->id);
                    live_add(union_vars, live_in, interval->var);
                }
                interval_add_use_pos(c, interval, op->id, use_kind_of_input(c, op, interval));
            }

            current = current->prev;
        }

        // live in 中不能包含 phi output
        current = list_first(block->operations)->succ;
        while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
            lir_op_t *op = OP(current);

            lir_operand_var *var = op->output->value;
            live_remove(union_vars, live_in, var);
            current = current->succ;
        }

        // TODO handle loop header add range

    }
}

interval_t *interval_new(closure_t *c) {
    interval_t *i = malloc(sizeof(interval_t));
    i->ranges = list_new();
    i->use_positions = list_new();
    i->children = list_new();
    i->stack_slot = NEW(int);
    *i->stack_slot = -1;
    i->spilled = false;
    i->fixed = false;
    i->parent = NULL;
    i->index = c->interval_count++; // 基于 closure_t 做自增 id 即可
    return i;
}

bool interval_is_covers(interval_t *i, int position) {
    list_node *current = list_first(i->ranges);
    while (current->value != NULL) {
        interval_range_t *range = current->value;
        if (range->from <= position && range->to >= position) {
            return true;
        }

        current = current->succ;
    }
    return 0;
}

int interval_next_intersection(interval_t *current, interval_t *select) {
    int position = current->first_range->from; // first_from 指向 range 的开头
    while (position < current->last_range->to) {
        if (interval_is_covers(current, position) && interval_is_covers(select, position)) {
            return position;
        }
        position++;
    }

    return position;
}

// 在 before 前挑选一个最佳的位置进行 split
int interval_find_optimal_split_pos(closure_t *c, interval_t *current, int before) {
    return before;
}

/**
 * add range 总是基于 block_from ~ input.id,
 * 且从后往前遍历，所以只需要根据 to 和 first_from 比较，就能判断出是否重叠
 * @param c
 * @param i
 * @param from
 * @param to
 */
void interval_add_range(closure_t *c, interval_t *i, int from, int to) {
    assert(from < to);

    if (i->first_range->from <= to) {
        // form 选小的， to 选大的
        if (from < i->first_range->from) {
            i->first_range->from = from;
        }
        if (to > i->last_range->to) {
            i->last_range->to = to;
        }
    } else {
        // 不重叠,则 range 插入到 ranges 的最前面
        interval_range_t *range = NEW(interval_range_t);
        range->from = from;
        range->to = to;

        list_insert_before(i->ranges, NULL, range);
        i->first_range = range;
        if (i->ranges->count == 1) {
            i->last_range = range;
        }
    }
}

/**
 * 按从小到大排序
 * @param c
 * @param i
 * @param position
 * @param kind
 */
void interval_add_use_pos(closure_t *c, interval_t *i, int position, use_kind_e kind) {
    list *pos_list = i->use_positions;

    use_pos_t *new_pos = NEW(use_pos_t);
    new_pos->kind = kind;
    new_pos->value = position;

    list_node *current = list_first(pos_list);
    while (current->value != NULL) {
        use_pos_t *current_pos = current->value;
        // 找到一个大于当前位置的节点
        if (current_pos->value > new_pos->value) {
            // 当前位置一旦大于 await
            // 就表示 current->prev < await < current
            // 或者 await < current, 也就是 current 就是第一个元素
            list_insert_after(pos_list, current->prev, new_pos);
            return;
        }

        current = current->succ;
    }
}


int interval_next_use_position(interval_t *i, int after_position) {
    list *pos_list = i->use_positions;

    LIST_FOR(pos_list) {
        use_pos_t *current_pos = LIST_VALUE();
        if (current_pos->value > after_position) {
            return current_pos->value;
        }
    }
    return 0;
}

/**
 * 从 position 将 interval 分成两端，多个 child interval 在一个 list 中，而不是多级 list
 * 如果 position 被 range cover, 则对 range 进行切分
 * child 的 register_hint 指向 parent
 * @param c
 * @param i
 * @param position
 */
interval_t *interval_split_at(closure_t *c, interval_t *i, int position) {
    assert(i->last_range->to < position);
    assert(i->first_range->from < position);

    interval_t *child = interval_new_child(c, i);

    // mov id = position - 1
    closure_insert_mov(c, position - 1, i, child);

    // 将 child 加入 parent 的 children 中,
    // 因为是从 i 中分割出来的，所以需要插入到 i 对应到 node 的后方
    interval_t *parent = i;
    if (parent->parent) {
        parent = parent->parent;
    }
    if (list_empty(parent->children)) {
        list_push(parent->children, child);
    } else {
        LIST_FOR(parent->children) {
            interval_t *current = LIST_VALUE();
            if (current->index == i->index) {
                list_insert_after(parent->children, LIST_NODE(), child);
                break;
            }
        }
    }

    // 切割 range, TODO first_range.from must == first def position?
    LIST_FOR(i->ranges) {
        interval_range_t *range = LIST_VALUE();
        if (!in_range(range, position)) {
            continue;
        }

        // 如果 position 在 range 的起始位置，则直接将 ranges list 的当前部分和剩余部分分给 child interval 即可
        // 否则 对 range 进行切割
        // tips: position 必定不等于 range->to, 因为 to 是 excluded
        if (position == range->from) {
            child->ranges = list_split(i->ranges, LIST_NODE());
        } else {
            interval_range_t *new_range = NEW(interval_range_t);
            new_range->from = position;
            new_range->to = range->to;
            range->to = position;

            // 将 new_range 插入到 ranges 中
            list_insert_after(i->ranges, LIST_NODE(), new_range);
            child->ranges = list_split(i->ranges, LIST_NODE());
        }

        break;
    }

    // 划分 position
    LIST_FOR(i->use_positions) {
        use_pos_t *pos = LIST_VALUE();
        if (pos->value < position) {
            continue;
        }

        // pos->value >= position, pos 和其之后的 pos 都需要加入到 new child 中
        child->use_positions = list_split(i->use_positions, LIST_NODE());
        break;
    }


    return child;
}

/**
 * spill slot，清空 assign
 * 所有 slit_child 都溢出到同一个堆栈插槽（存储在_canonical_spill_slot中）
 * @param i
 */
void interval_spill_slot(closure_t *c, interval_t *i) {
    assert(i->stack_slot);

    i->assigned = 0;
    i->spilled = true;
    if (*i->stack_slot != -1) {
        return;
    }
    // 根据 closure stack offset 分配堆栈插槽,暂时不用考虑对其，直接从 0 开始分配即可
    *i->stack_slot = c->stack_slot;
    c->stack_slot += type_base_sizeof(i->var->type_base);
}

/**
 * use_positions 是否包含 kind > 0 的position, 有则返回 use_position，否则返回 NULL
 * @param i
 * @return
 */
use_pos_t *interval_must_reg_pos(interval_t *i) {
    LIST_FOR(i->use_positions) {
        use_pos_t *pos = LIST_VALUE();
        if (pos->kind == USE_KIND_MUST) {
            return pos;
        }
    }
    return NULL;
}


void resolve_data_flow(closure_t *c) {
    SLICE_FOR(c->blocks, basic_block_t) {
        basic_block_t *from = SLICE_VALUE();
        for (int i = 0; i < from->succs->count; ++i) {
            basic_block_t *to = from->succs->take[i];

            resolver_t r = {
                    .from_list = slice_new(),
                    .to_list = slice_new(),
                    .insert_block = NULL,
                    .insert_id = 0,
            };

            // to 入口活跃则可能存在对同一个变量在进入到当前块之前就已经存在了，所以可能会进行 spill/reload
            // for each interval it live at begin of successor do ? 怎么拿这样的 interval? 最简单办法是通过 live_in
            // live_in not contain phi def interval
            for (int j = 0; j < to->live_in->count; ++j) {
                lir_operand_var *var = to->live_in->take[j];
                interval_t *parent_interval = table_get(c->interval_table, var->ident);
                assert(parent_interval);

                // 判断是否在 form->to edge 最终的 interval
                interval_t *from_interval = interval_child_at(parent_interval, from->last_op->id);
                interval_t *to_interval = interval_child_at(parent_interval, to->first_op->id);
                // 因为 from 和 interval 是相连接的 edge,
                // 如果from_interval != to_interval(指针对比即可)
                // 则说明在其他 edge 上对 interval 进行了 spilt/reload
                // 因此需要 link from and to interval
                if (from_interval != to_interval) {
                    slice_push(r.from_list, from_interval);
                    slice_push(r.to_list, to_interval);
                }
            }

            // phi def interval(label op -> phi op -> ... -> phi op -> other op)
            list_node *current = list_first(to->operations)->succ;
            while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
                lir_op_t *op = OP(current);
                //  to phi.inputOf(pred def) will is from interval
                // TODO ssa body constant handle
                lir_operand_var *var = ssa_phi_body_of(op->first->value, to->preds, from);
                interval_t *temp_interval = table_get(c->interval_table, var->ident);
                assert(temp_interval);
                interval_t *from_interval = interval_child_at(temp_interval, from->last_op->id);

                lir_operand_var *def = op->output->value; // result must assign reg
                temp_interval = table_get(c->interval_table, def->ident);
                assert(temp_interval);
                interval_t *to_interval = interval_child_at(temp_interval, to->first_op->id);
                // 因为 from 和 interval 是相连接的 edge,
                // 如果from_interval != to_interval(指针对比即可)
                // 则说明在其他 edge 上对 interval 进行了 spilt/reload
                // 因此需要 link from and to interval
                if (from_interval != to_interval) {
                    slice_push(r.from_list, from_interval);
                    slice_push(r.to_list, to_interval);
                }

                current = current->succ;
            }


            // 对一条边的处理完毕，可能涉及多个寄存器，stack, 也有可能一个寄存器被多次操作,所以需要处理覆盖问题
            // 同一条边上的所有 resolve 操作只插入在同一块中，要么是在 from、要么是在 to。 tips: 所有的 critical edge 都已经被处理了
            resolve_find_insert_pos(&r, from, to);

            // 按顺序插入 move
            resolve_mappings(c, &r);
        }
    }
}

interval_t *interval_child_at(interval_t *i, int op_id) {
    if (list_empty(i->children)) {
        return i;
    }

    if (i->first_range->from <= op_id && i->last_range->to > op_id) {
        return i;
    }


    LIST_FOR(i->children) {
        interval_t *child = LIST_VALUE();
        if (child->first_range->from <= op_id && child->last_range->to > op_id) {
            return child;
        }
    }

    assert(false && "op_id not in interval");
}

/**
 * 由于 ssa resolve 的存在，所以存在从 interval A(stack A) 移动到 interval B(stackB)
 * 但是大多数寄存器不支持从 stack 移动到 stack，所以必须给 phi def 添加一个寄存器，也就是 use_pos kind = MUST
 *
 * 可能存在类似下面这样的冲突，此时无论先操作哪个移动指令都会造成 overwrite
 * mov rax -> rcx
 * mov rcx -> rax
 * 修改为
 * mov rax -> stack
 * mov stack -> rcx
 * mov rcx -> rax
 * @param c
 * @param r
 */
void resolve_mappings(closure_t *c, resolver_t *r) {
    if (r->from_list->count == 0) {
        return;
    }

    // block all from interval, value 保持被引用的次数
    int8_t block_regs[UINT8_MAX] = {0};
    SLICE_FOR(r->from_list, interval_t) {
        interval_t *i = SLICE_VALUE();
        if (i->assigned) {
            block_regs[i->assigned] += 1;
        }
    }

    int spill_candidate = -1;
    while (r->from_list > 0) {
        bool processed = false;
        for (int i = 0; i < r->from_list->count; ++i) {
            interval_t *from = r->from_list->take[i];
            interval_t *to = r->to_list->take[i];

            if (resolve_blocked(block_regs, from, to)) {
                // this interval cannot be processed now because target is not free
                // it starts in a register, so it is a possible candidate for spilling
                spill_candidate = i;
                continue;
            }

            block_insert_mov(r->insert_block, r->insert_id, from, to);

            if (from->assigned) {
                block_regs[from->assigned] -= 1;
            }

            slice_remove(r->from_list, i);
            slice_remove(r->to_list, i);

            processed = true;
        }

        // 已经卡死了，那进行再多次尝试也是没有意义的
        if (!processed) {
            assert(spill_candidate != -1 && "cannot resolve mappings");
            interval_t *from = r->from_list->take[spill_candidate];
            interval_t *spill_child = interval_new_child(c, from);
            interval_add_range(c, spill_child, 1, 2);
            interval_spill_slot(c, spill_child);

            // insert mov
            block_insert_mov(r->insert_block, r->insert_id, from, spill_child);

            // from update
            r->from_list->take[spill_candidate] = spill_child;

            // unlock interval reg
            block_regs[from->assigned] -= 1;
        }
    }

}

void resolve_find_insert_pos(resolver_t *r, basic_block_t *from, basic_block_t *to) {
    if (from->succs->count <= 1) {
        // from only one succ
        // insert before branch
        r->insert_block = from;

        lir_op_t *last_op = from->last_op;
        if (lir_op_is_branch(last_op)) {
            // insert before last op
            r->insert_id = last_op->id - 1;
        } else {
            r->insert_id = last_op->id + 1;
        }

    } else {
        r->insert_block = to;
        r->insert_id = from->first_op->id - 1;
    }
}
