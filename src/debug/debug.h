#ifndef NATURE_SRC_DEBUG_DEBUG_H_
#define NATURE_SRC_DEBUG_DEBUG_H_

#include "src/value.h"
#include "src/ast.h"
#include "src/syntax/token.h"
#include "src/lir.h"

#define DEBUG_STR_COUNT 1000

//#define DEBUG_SCANNER

//#define DEBUG_PARSER

//#define DEBUG_ANALYSIS

//#define DEBUG_INFER

#define DEBUG_COMPILER

void debug_scanner(token *t);

void debug_parser(int line, string token);

void debug_parser_stmt(ast_stmt_expr_type t);

void debug_stmt(string type, ast_stmt stmt);

void debug_lir(int lir_line, lir_op *op);

#endif //NATURE_SRC_DEBUG_DEBUG_H_
