#include "intervals.h"
#include "src/lib/list.h"
#include <string.h>
#include <stdlib.h>

// 每个块需要存储什么数据？
// loop flag
// loop index,只存储最内层的循环的 index
// loop depth
// 每个块的前向和后向分支 count
// 当且仅当循环块的开头
// 假如使用广度优先遍历，编号按照广度优先的层级来编号,则可以方便的计算出树的高度,顶层 为 0，然后依次递增
// 使用树的高度来标记 loop_index，如果一个块被标记了两个 index ,则 index 大的为内嵌循环
// 当前层 index 等于父节点 + 1
void intervals_loop_detection(closure *c) {
  c->entry->loop.flag = LOOP_DETECTION_FLAG_VISITED;
  c->entry->loop.tree_high = 1; // 从 1 开始标号，避免出现 0 = 0 的判断
  list *work_list = list_new();
  list_push(work_list, c->entry);

  lir_blocks loop_headers = {.count = 0};
  lir_blocks loop_ends = {.count = 0};

  // 1. 探测出循环头与循环尾部
  while (!list_empty(work_list)) {
    lir_basic_block *block = list_pop(work_list);

    // 是否会出现 succ 的 flag 是 visited?
    // 如果当前块是 visited,则当前块的正向后继一定是 null, 当前块的反向后继一定是 active,不可能是 visited
    // 因为一个块的所有后继都进入到 work_list 之后，才会进行下一次 work_list 提取操作
    for (int i = 0; i < block->succs.count; ++i) {
      lir_basic_block *succ = block->succs.list[i];
      succ->loop.tree_high = block->loop.tree_high + 1;

      // 如果发现循环, backward branches
      if (succ->loop.flag == LOOP_DETECTION_FLAG_ACTIVE) {
        // 当前 succ 是 loop_headers, loop_headers 的 tree_high 为 loop_index
        succ->loop.index = succ->loop.tree_high;
        succ->loop.index_list[succ->loop.depth++] = succ->loop.tree_high;
        loop_headers.list[loop_headers.count++] = succ;

        // 当前 block 是 loop_ends, loop_ends, index = loop_headers.index
        block->loop.index = succ->loop.tree_high;
        block->loop.index_list[block->loop.depth++] = succ->loop.tree_high;
        loop_ends.list[loop_ends.count++] = block;
        continue;
      }

      succ->loop.flag = LOOP_DETECTION_FLAG_VISITED;
    }

    // 变更 flag
    block->loop.flag = LOOP_DETECTION_FLAG_ACTIVE;
  }

  // 2. 标号, 这里有一个严肃的问题，如果一个节点有两个前驱时，也能够被标号吗？如果是普通结构不考虑 goto 的情况下，则不会初选这种 cfg
  for (int i = 0; i < loop_ends.count; ++i) {
    lir_basic_block *end = loop_ends.list[i];
    list_push(work_list, end);
    table *exist_table = table_new();
    table_set(exist_table, lir_label_to_string(end->label), end);

    while (!list_empty(work_list)) {
      lir_basic_block *block = list_pop(work_list);
      if (block->label != end->label && block->loop.index == end->loop.index) {
        continue;
      }
      // 标号
      block->loop.index_list[block->loop.depth++] = end->loop.index;

      for (int k = 0; k < block->preds.count; ++k) {
        lir_basic_block *pred = block->preds.list[k];

        // 判断是否已经入过队(标号)
        if (table_exist(exist_table, lir_label_to_string(pred->label))) {
          continue;
        }
        table_set(exist_table, lir_label_to_string(block->label), block);
        list_push(work_list, pred);
      }
    }
    table_free(exist_table);
  }

  // 3. 遍历所有 basic_block ,通过 loop.index_list 确定 index
  for (int label = 0; label < c->blocks.count; ++label) {
    lir_basic_block *block = c->blocks.list[label];
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
